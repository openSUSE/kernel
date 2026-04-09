// SPDX-License-Identifier: GPL-2.0 OR MIT

use super::*;

/// The actual data that gets threaded through the callbacks.
struct SmData<'a, 'ctx, T: DriverGpuVm> {
    gpuvm: &'a mut UniqueRefGpuVm<T>,
    user_context: &'a mut T::SmContext<'ctx>,
}

/// Represents an `sm_step_unmap` operation that has not yet been completed.
pub struct OpUnmap<'op, T: DriverGpuVm> {
    op: &'op bindings::drm_gpuva_op_unmap,
    // This ensures that 'op is invariant, so that `OpUnmap<'long, T>` does not
    // coerce to `OpUnmap<'short, T>`. This ensures that the user can't return the
    // wrong`OpUnmapped` value.
    _invariant: PhantomData<*mut &'op mut T>,
}

impl<'op, T: DriverGpuVm> OpUnmap<'op, T> {
    /// Indicates whether this [`GpuVa`] is physically contiguous with the
    /// original mapping request.
    ///
    /// Optionally, if `keep` is set, drivers may keep the actual page table
    /// mappings for this `drm_gpuva`, adding the missing page table entries
    /// only and update the `drm_gpuvm` accordingly.
    pub fn keep(&self) -> bool {
        self.op.keep
    }

    /// The range being unmapped.
    pub fn va(&self) -> &GpuVa<T> {
        // SAFETY: This is a valid va. It's not the `kernel_alloc_node` because you can't unmap it,
        // and it's not sparse by the `GpuVm<T>` type invariants.
        unsafe { GpuVa::<T>::from_raw(self.op.va) }
    }

    /// Remove the VA.
    pub fn remove(self) -> (OpUnmapped<'op, T>, GpuVaRemoved<T>) {
        // SAFETY: The op references a valid drm_gpuva in the GPUVM.
        unsafe { bindings::drm_gpuva_unmap(self.op) };
        // SAFETY: The va is no longer in the interval tree so we may unlink it.
        unsafe { bindings::drm_gpuva_unlink_defer(self.op.va) };

        // SAFETY: We just removed this va from the `GpuVm<T>`.
        let va = unsafe { GpuVaRemoved::from_raw(self.op.va) };

        (
            OpUnmapped {
                _invariant: self._invariant,
            },
            va,
        )
    }
}

/// Represents a completed [`OpUnmap`] operation.
pub struct OpUnmapped<'op, T> {
    _invariant: PhantomData<*mut &'op mut T>,
}

/// Represents an `sm_step_remap` operation that has not yet been completed.
pub struct OpRemap<'op, T: DriverGpuVm> {
    op: &'op bindings::drm_gpuva_op_remap,
    // This ensures that 'op is invariant, so that `OpRemap<'long, T>` does not
    // coerce to `OpRemap<'short, T>`. This ensures that the user can't return the
    // wrong`OpRemapped` value.
    _invariant: PhantomData<*mut &'op mut T>,
}

impl<'op, T: DriverGpuVm> OpRemap<'op, T> {
    /// The preceding part of a split mapping.
    #[inline]
    pub fn prev(&self) -> Option<&OpRemapMapData> {
        // SAFETY: We checked for null, so the pointer must be valid.
        NonNull::new(self.op.prev).map(|ptr| unsafe { OpRemapMapData::from_raw(ptr) })
    }

    /// The subsequent part of a split mapping.
    #[inline]
    pub fn next(&self) -> Option<&OpRemapMapData> {
        // SAFETY: We checked for null, so the pointer must be valid.
        NonNull::new(self.op.next).map(|ptr| unsafe { OpRemapMapData::from_raw(ptr) })
    }

    /// Indicates whether the `drm_gpuva` being removed is physically contiguous with the original
    /// mapping request.
    ///
    /// Optionally, if `keep` is set, drivers may keep the actual page table mappings for this
    /// `drm_gpuva`, adding the missing page table entries only and update the `drm_gpuvm`
    /// accordingly.
    #[inline]
    pub fn keep(&self) -> bool {
        // SAFETY: The unmap pointer is always valid.
        unsafe { (*self.op.unmap).keep }
    }

    /// The range being unmapped.
    #[inline]
    pub fn va_to_unmap(&self) -> &GpuVa<T> {
        // SAFETY: This is a valid va. It's not the `kernel_alloc_node` because you can't unmap it,
        // and it's not sparse by the `GpuVm<T>` type invariants.
        unsafe { GpuVa::<T>::from_raw((*self.op.unmap).va) }
    }

    /// The [`drm_gem_object`](DriverGpuVm::Object) whose VA is being remapped.
    #[inline]
    pub fn obj(&self) -> &T::Object {
        self.va_to_unmap().obj()
    }

    /// The [`GpuVmBo`] that is being remapped.
    #[inline]
    pub fn vm_bo(&self) -> &GpuVmBo<T> {
        self.va_to_unmap().vm_bo()
    }

    /// Update the GPUVM to perform the remapping.
    pub fn remap(
        self,
        va_alloc: [GpuVaAlloc<T>; 2],
        prev_data: impl PinInit<T::VaData>,
        next_data: impl PinInit<T::VaData>,
    ) -> (OpRemapped<'op, T>, OpRemapRet<T>) {
        let [va1, va2] = va_alloc;

        let mut unused_va = None;
        let mut prev_ptr = ptr::null_mut();
        let mut next_ptr = ptr::null_mut();
        if self.prev().is_some() {
            prev_ptr = va1.prepare(prev_data);
        } else {
            unused_va = Some(va1);
        }
        if self.next().is_some() {
            next_ptr = va2.prepare(next_data);
        } else {
            unused_va = Some(va2);
        }

        // SAFETY: the pointers are non-null when required
        unsafe { bindings::drm_gpuva_remap(prev_ptr, next_ptr, self.op) };

        let gpuva_guard = self.vm_bo().lock_gpuva();
        if !prev_ptr.is_null() {
            // SAFETY: The prev_ptr is a valid drm_gpuva prepared for insertion. The vm_bo is still
            // valid as the not-yet-unlinked gpuva holds a refcount on the vm_bo.
            unsafe { bindings::drm_gpuva_link(prev_ptr, self.vm_bo().as_raw()) };
        }
        if !next_ptr.is_null() {
            // SAFETY: The next_ptr is a valid drm_gpuva prepared for insertion. The vm_bo is still
            // valid as the not-yet-unlinked gpuva holds a refcount on the vm_bo.
            unsafe { bindings::drm_gpuva_link(next_ptr, self.vm_bo().as_raw()) };
        }
        drop(gpuva_guard);

        // SAFETY: The va is no longer in the interval tree so we may unlink it.
        unsafe { bindings::drm_gpuva_unlink_defer((*self.op.unmap).va) };

        (
            OpRemapped {
                _invariant: self._invariant,
            },
            OpRemapRet {
                // SAFETY: We just removed this va from the `GpuVm<T>`.
                unmapped_va: unsafe { GpuVaRemoved::from_raw((*self.op.unmap).va) },
                unused_va,
            },
        )
    }
}

/// Part of an [`OpRemap`] that represents a new mapping.
#[repr(transparent)]
pub struct OpRemapMapData(bindings::drm_gpuva_op_map);

impl OpRemapMapData {
    /// # Safety
    /// Must reference a valid `drm_gpuva_op_map` for duration of `'a`.
    unsafe fn from_raw<'a>(ptr: NonNull<bindings::drm_gpuva_op_map>) -> &'a Self {
        // SAFETY: ok per safety requirements
        unsafe { ptr.cast().as_ref() }
    }

    /// The base address of the new mapping.
    pub fn addr(&self) -> u64 {
        self.0.va.addr
    }

    /// The length of the new mapping.
    pub fn length(&self) -> u64 {
        self.0.va.range
    }

    /// The offset within the [`drm_gem_object`](DriverGpuVm::Object).
    pub fn gem_offset(&self) -> u64 {
        self.0.gem.offset
    }
}

/// Struct containing objects removed or not used by [`OpRemap::remap`].
pub struct OpRemapRet<T: DriverGpuVm> {
    /// The `drm_gpuva` that was removed.
    pub unmapped_va: GpuVaRemoved<T>,
    /// If the remap did not split the region into two pieces, then the unused `drm_gpuva` is
    /// returned here.
    pub unused_va: Option<GpuVaAlloc<T>>,
}

/// Represents a completed [`OpRemap`] operation.
pub struct OpRemapped<'op, T> {
    _invariant: PhantomData<*mut &'op mut T>,
}

impl<T: DriverGpuVm> UniqueRefGpuVm<T> {
    /// Remove any mappings in the given region.
    ///
    /// Internally calls [`DriverGpuVm::sm_step_unmap`] for ranges entirely contained within the
    /// given range, and [`DriverGpuVm::sm_step_remap`] for ranges that overlap with the range.
    #[inline]
    pub fn sm_unmap(&mut self, addr: u64, length: u64, context: &mut T::SmContext<'_>) -> Result {
        let gpuvm = self.as_raw();
        let mut p = SmData {
            gpuvm: self,
            user_context: context,
        };
        // SAFETY:
        // * raw_request() creates a valid request.
        // * The private data is valid to be interpreted as SmData.
        to_result(unsafe { bindings::drm_gpuvm_sm_unmap(gpuvm, (&raw mut p).cast(), addr, length) })
    }
}

impl<T: DriverGpuVm> GpuVm<T> {
    /// # Safety
    /// Must be called from `sm_unmap` with a pointer to `SmData`.
    pub(super) unsafe extern "C" fn sm_step_unmap(
        op: *mut bindings::drm_gpuva_op,
        p: *mut c_void,
    ) -> c_int {
        // SAFETY: The caller provides a pointer to `SmData`.
        let p = unsafe { &mut *p.cast::<SmData<'_, '_, T>>() };
        let op = OpUnmap {
            // SAFETY: sm_step_unmap is called with an unmap operation.
            op: unsafe { &(*op).__bindgen_anon_1.unmap },
            _invariant: PhantomData,
        };
        match p.gpuvm.data().sm_step_unmap(op, p.user_context) {
            Ok(OpUnmapped { .. }) => 0,
            Err(err) => err.to_errno(),
        }
    }

    /// # Safety
    /// Must be called from `sm_unmap` with a pointer to `SmData`.
    pub(super) unsafe extern "C" fn sm_step_remap(
        op: *mut bindings::drm_gpuva_op,
        p: *mut c_void,
    ) -> c_int {
        // SAFETY: The caller provides a pointer to `SmData`.
        let p = unsafe { &mut *p.cast::<SmData<'_, '_, T>>() };
        let op = OpRemap {
            // SAFETY: sm_step_remap is called with a remap operation.
            op: unsafe { &(*op).__bindgen_anon_1.remap },
            _invariant: PhantomData,
        };
        match p.gpuvm.data().sm_step_remap(op, p.user_context) {
            Ok(OpRemapped { .. }) => 0,
            Err(err) => err.to_errno(),
        }
    }
}

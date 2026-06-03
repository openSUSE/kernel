// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! FSP (Foundation Security Processor) interface for Hopper/Blackwell GPUs.
//!
//! Hopper/Blackwell use a simplified firmware boot sequence: FMC, then FSP, then GSP.
//! Unlike Turing/Ampere/Ada, there is no SEC2 (Security Engine 2) usage.
//! FSP handles secure boot directly using FMC firmware and Chain of Trust.

use kernel::{
    device,
    io::poll::read_poll_timeout,
    prelude::*,
    time::Delta,
    transmute::{
        AsBytes,
        FromBytes, //
    },
};

use crate::{
    driver::Bar0,
    falcon::{
        fsp::Fsp as FspEngine,
        Falcon, //
    },
    firmware::fsp::FspFirmware,
    gpu::Chipset,
    mctp::{
        MctpHeader,
        NvdmHeader,
        NvdmType, //
    },
    regs, //
};

mod hal;

/// FSP command response payload (`NVDM_PAYLOAD_COMMAND_RESPONSE`).
#[repr(C, packed)]
#[derive(Clone, Copy)]
struct NvdmPayloadCommandResponse {
    task_id: u32,
    command_nvdm_type: u32,
    error_code: u32,
}

/// Complete FSP response structure with MCTP and NVDM headers.
#[repr(C, packed)]
#[derive(Clone, Copy)]
struct FspResponse {
    mctp_header: MctpHeader,
    nvdm_header: NvdmHeader,
    response: NvdmPayloadCommandResponse,
}

// SAFETY: FspResponse is a packed C struct with only integral fields.
unsafe impl FromBytes for FspResponse {}

/// Trait implemented by types representing a message to send to FSP.
///
/// This provides [`Fsp::send_sync_fsp`] with the information it needs to send
/// a given message, following the same pattern as GSP's `CommandToGsp`.
trait MessageToFsp: AsBytes {
    /// NVDM type identifying this message to FSP.
    const NVDM_TYPE: NvdmType;
}

/// FSP interface for Hopper/Blackwell GPUs.
///
/// An `Fsp` is produced by [`Fsp::wait_secure_boot`], which only returns once FSP secure boot
/// has completed. It owns the FSP falcon and the FMC firmware, which are used for the subsequent
/// Chain of Trust boot.
pub(crate) struct Fsp {
    falcon: Falcon<FspEngine>,
    #[expect(dead_code)]
    fsp_fw: FspFirmware,
}

impl Fsp {
    /// Waits for FSP secure boot completion, then returns the [`Fsp`] interface.
    ///
    /// Polls the thermal scratch register until FSP signals boot completion or the timeout
    /// elapses. Returning an [`Fsp`] only on success guarantees, at the API level, that the
    /// interface is not used before secure boot has completed.
    pub(crate) fn wait_secure_boot(
        dev: &device::Device<device::Bound>,
        bar: &Bar0,
        chipset: Chipset,
        fsp_fw: FspFirmware,
    ) -> Result<Fsp> {
        /// FSP secure boot completion timeout in milliseconds.
        const FSP_SECURE_BOOT_TIMEOUT_MS: i64 = 5000;

        let hal = hal::fsp_hal(chipset).ok_or(ENOTSUPP)?;
        let falcon = Falcon::<FspEngine>::new(dev, chipset)?;

        read_poll_timeout(
            || Ok(hal.fsp_boot_status(bar)),
            |&status| status == regs::NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE_STATUS_SUCCESS,
            Delta::from_millis(10),
            Delta::from_millis(FSP_SECURE_BOOT_TIMEOUT_MS),
        )
        .inspect_err(|e| {
            dev_err!(dev, "FSP secure boot completion error: {:?}\n", e);
        })?;

        Ok(Fsp { falcon, fsp_fw })
    }

    /// Sends a message to FSP and waits for the response.
    #[expect(dead_code)]
    fn send_sync_fsp<M>(&mut self, dev: &device::Device, bar: &Bar0, msg: &M) -> Result
    where
        M: MessageToFsp,
    {
        self.falcon.send_msg(bar, msg.as_bytes())?;

        let response_buf = self.falcon.recv_msg(bar).inspect_err(|e| {
            dev_err!(dev, "FSP response error: {:?}\n", e);
        })?;

        let (response, _) = FspResponse::from_bytes_prefix(&response_buf[..]).ok_or_else(|| {
            dev_err!(dev, "FSP response too small: {}\n", response_buf.len());
            EIO
        })?;

        let mctp_header = response.mctp_header;
        let nvdm_header = response.nvdm_header;
        let command_nvdm_type = response.response.command_nvdm_type;
        let error_code = response.response.error_code;

        if !mctp_header.is_single_packet() {
            dev_err!(
                dev,
                "Unexpected MCTP header in FSP reply: {:x?}\n",
                mctp_header,
            );
            return Err(EIO);
        }

        if !nvdm_header.validate(NvdmType::FspResponse) {
            dev_err!(
                dev,
                "Unexpected NVDM header in FSP reply: {:x?}\n",
                nvdm_header,
            );
            return Err(EIO);
        }

        if command_nvdm_type != u8::from(M::NVDM_TYPE).into() {
            dev_err!(
                dev,
                "Expected NVDM type {:?} in reply, got {:#x}\n",
                M::NVDM_TYPE,
                command_nvdm_type
            );
            return Err(EIO);
        }

        if error_code != 0 {
            dev_err!(
                dev,
                "NVDM command {:?} failed with error {:#x}\n",
                M::NVDM_TYPE,
                error_code
            );
            return Err(EIO);
        }

        Ok(())
    }
}

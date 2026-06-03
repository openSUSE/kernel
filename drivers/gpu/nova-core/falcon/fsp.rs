// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

//! FSP (Foundation Security Processor) falcon engine for Hopper/Blackwell GPUs.
//!
//! The FSP falcon handles secure boot and Chain of Trust operations
//! on Hopper and Blackwell architectures, replacing SEC2's role.

use kernel::{
    io::{
        register::{
            RegisterBase,
            WithBase, //
        },
        Io, //
    },
    prelude::*,
};

use crate::{
    driver::Bar0,
    falcon::{
        Falcon,
        FalconEngine,
        PFalcon2Base,
        PFalconBase, //
    },
    regs,
};

/// Type specifying the `Fsp` falcon engine. Cannot be instantiated.
pub(crate) struct Fsp(());

impl RegisterBase<PFalconBase> for Fsp {
    const BASE: usize = 0x8f2000;
}

impl RegisterBase<PFalcon2Base> for Fsp {
    const BASE: usize = 0x8f3000;
}

impl FalconEngine for Fsp {}

impl Falcon<Fsp> {
    /// Writes `data` to FSP external memory at offset `0`.
    ///
    /// `data` is interpreted as little-endian 32-bit words. Returns `EINVAL`
    /// if the `data` length is not 4-byte aligned.
    #[expect(dead_code)]
    fn write_emem(&mut self, bar: &Bar0, data: &[u8]) -> Result {
        if data.len() % 4 != 0 {
            return Err(EINVAL);
        }

        // Begin a write burst at offset `0`, auto-incrementing on each write.
        bar.write(
            WithBase::of::<Fsp>(),
            regs::NV_PFALCON_FALCON_EMEMC::zeroed().with_aincw(true),
        );

        for chunk in data.chunks_exact(4) {
            let value = u32::from_le_bytes([chunk[0], chunk[1], chunk[2], chunk[3]]);

            // Write the next 32-bit `value`; hardware advances the offset.
            bar.write(
                WithBase::of::<Fsp>(),
                regs::NV_PFALCON_FALCON_EMEMD::zeroed().with_data(value),
            );
        }

        Ok(())
    }

    /// Reads FSP external memory from offset `0` into `data`.
    ///
    /// `data` is stored as little-endian 32-bit words. Returns `EINVAL` if
    /// the `data` length is not 4-byte aligned.
    #[expect(dead_code)]
    fn read_emem(&mut self, bar: &Bar0, data: &mut [u8]) -> Result {
        if data.len() % 4 != 0 {
            return Err(EINVAL);
        }

        // Begin a read burst at offset `0`, auto-incrementing on each read.
        bar.write(
            WithBase::of::<Fsp>(),
            regs::NV_PFALCON_FALCON_EMEMC::zeroed().with_aincr(true),
        );

        for chunk in data.chunks_exact_mut(4) {
            // Read the next 32-bit word; hardware advances the offset.
            let value = bar.read(regs::NV_PFALCON_FALCON_EMEMD::of::<Fsp>()).data();
            chunk.copy_from_slice(&value.to_le_bytes());
        }

        Ok(())
    }
}

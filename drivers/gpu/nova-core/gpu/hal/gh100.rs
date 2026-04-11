// SPDX-License-Identifier: GPL-2.0

use kernel::prelude::*;

use crate::driver::Bar0;

use super::GpuHal;

struct Gh100;

impl GpuHal for Gh100 {
    fn wait_gfw_boot_completion(&self, _bar: &Bar0) -> Result {
        Ok(())
    }
}

const GH100: Gh100 = Gh100;
pub(super) const GH100_HAL: &dyn GpuHal = &GH100;

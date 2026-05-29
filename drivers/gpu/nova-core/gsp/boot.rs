// SPDX-License-Identifier: GPL-2.0

use kernel::{
    device,
    dma::Coherent,
    io::poll::read_poll_timeout,
    pci,
    prelude::*,
    time::Delta, //
};

use crate::{
    driver::Bar0,
    falcon::{
        gsp::Gsp,
        sec2::Sec2,
        Falcon, //
    },
    fb::FbLayout,
    firmware::{
        gsp::GspFirmware,
        FIRMWARE_VERSION, //
    },
    gpu::Chipset,
    gsp::{
        commands,
        GspFwWprMeta, //
    },
};

impl super::Gsp {
    /// Attempt to boot the GSP.
    ///
    /// This is a GPU-dependent and complex procedure that involves loading firmware files from
    /// user-space, patching them with signatures, and building firmware-specific intricate data
    /// structures that the GSP will use at runtime.
    ///
    /// Upon return, the GSP is up and running, and its runtime object given as return value.
    pub(crate) fn boot(
        self: Pin<&mut Self>,
        pdev: &pci::Device<device::Bound>,
        bar: &Bar0,
        chipset: Chipset,
        gsp_falcon: &Falcon<Gsp>,
        sec2_falcon: &Falcon<Sec2>,
    ) -> Result {
        let dev = pdev.as_ref();
        let hal = super::hal::gsp_hal(chipset);

        let gsp_fw = KBox::pin_init(GspFirmware::new(dev, chipset, FIRMWARE_VERSION), GFP_KERNEL)?;

        let fb_layout = FbLayout::new(chipset, bar, &gsp_fw)?;
        dev_dbg!(dev, "{:#x?}\n", fb_layout);

        let wpr_meta = Coherent::init(dev, GFP_KERNEL, GspFwWprMeta::new(&gsp_fw, &fb_layout))?;

        // Perform the chipset-specific boot sequence.
        hal.boot(
            &self,
            dev,
            bar,
            chipset,
            &fb_layout,
            &wpr_meta,
            gsp_falcon,
            sec2_falcon,
        )?;

        gsp_falcon.write_os_version(bar, gsp_fw.bootloader.app_version);

        // Poll for RISC-V to become active before continuing.
        read_poll_timeout(
            || Ok(gsp_falcon.is_riscv_active(bar)),
            |val: &bool| *val,
            Delta::from_millis(10),
            Delta::from_secs(5),
        )?;

        dev_dbg!(pdev, "RISC-V active? {}\n", gsp_falcon.is_riscv_active(bar),);

        self.cmdq
            .send_command_no_wait(bar, commands::SetSystemInfo::new(pdev))?;
        self.cmdq
            .send_command_no_wait(bar, commands::SetRegistry::new())?;

        hal.post_boot(&self, dev, bar, &gsp_fw, gsp_falcon, sec2_falcon)?;

        // Wait until GSP is fully initialized.
        commands::wait_gsp_init_done(&self.cmdq)?;

        // Obtain and display basic GPU information.
        let info = self.cmdq.send_command(bar, commands::GetGspStaticInfo)?;
        match info.gpu_name() {
            Ok(name) => dev_info!(pdev, "GPU name: {}\n", name),
            Err(e) => dev_warn!(pdev, "GPU name unavailable: {:?}\n", e),
        }

        Ok(())
    }
}

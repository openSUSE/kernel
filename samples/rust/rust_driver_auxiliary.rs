// SPDX-License-Identifier: GPL-2.0

//! Rust auxiliary driver sample (based on a PCI driver for QEMU's `pci-testdev`).
//!
//! To make this driver probe, QEMU must be run with `-device pci-testdev`.

use kernel::{
    auxiliary,
    device::{
        Bound,
        Core, //
    },
    driver,
    pci,
    prelude::*,
    types::ForLt,
    InPlaceModule, //
};

const MODULE_NAME: &CStr = <LocalModule as kernel::ModuleMetadata>::NAME;
const AUXILIARY_NAME: &CStr = c"auxiliary";

struct AuxiliaryDriver;

kernel::auxiliary_device_table!(
    AUX_TABLE,
    MODULE_AUX_TABLE,
    <AuxiliaryDriver as auxiliary::Driver>::IdInfo,
    [(auxiliary::DeviceId::new(MODULE_NAME, AUXILIARY_NAME), ())]
);

impl auxiliary::Driver for AuxiliaryDriver {
    type IdInfo = ();
    type Data<'bound> = Self;

    const ID_TABLE: auxiliary::IdTable<Self::IdInfo> = &AUX_TABLE;

    fn probe<'bound>(
        adev: &'bound auxiliary::Device<Core<'_>>,
        _info: &'bound Self::IdInfo,
    ) -> impl PinInit<Self, Error> + 'bound {
        dev_info!(
            adev,
            "Probing auxiliary driver for auxiliary device with id={}\n",
            adev.id()
        );

        ParentDriver::connect(adev)?;

        Ok(Self)
    }
}

struct Data {
    index: u32,
}

struct ParentDriver;

#[allow(clippy::type_complexity)]
struct ParentData<'bound> {
    _reg0: auxiliary::Registration<'bound, ForLt!(Data)>,
    _reg1: auxiliary::Registration<'bound, ForLt!(Data)>,
}

kernel::pci_device_table!(
    PCI_TABLE,
    MODULE_PCI_TABLE,
    <ParentDriver as pci::Driver>::IdInfo,
    [(pci::DeviceId::from_id(pci::Vendor::REDHAT, 0x5), ())]
);

impl pci::Driver for ParentDriver {
    type IdInfo = ();
    type Data<'bound> = ParentData<'bound>;

    const ID_TABLE: pci::IdTable<Self::IdInfo> = &PCI_TABLE;

    fn probe<'bound>(
        pdev: &'bound pci::Device<Core<'_>>,
        _info: &'bound Self::IdInfo,
    ) -> impl PinInit<Self::Data<'bound>, Error> + 'bound {
        Ok(ParentData {
            _reg0: auxiliary::Registration::new(
                pdev.as_ref(),
                AUXILIARY_NAME,
                0,
                MODULE_NAME,
                Data { index: 0 },
            )?,
            _reg1: auxiliary::Registration::new(
                pdev.as_ref(),
                AUXILIARY_NAME,
                1,
                MODULE_NAME,
                Data { index: 1 },
            )?,
        })
    }
}

impl ParentDriver {
    fn connect(adev: &auxiliary::Device<Bound>) -> Result {
        let dev = adev.parent();
        let pdev: &pci::Device<Bound> = dev.try_into()?;

        let data = adev.registration_data::<ForLt!(Data)>()?;

        dev_info!(
            dev,
            "Connect auxiliary {} with parent: VendorID={}, DeviceID={:#x}\n",
            adev.id(),
            pdev.vendor_id(),
            pdev.device_id()
        );

        dev_info!(
            dev,
            "Connected to auxiliary device with index {}.\n",
            data.index
        );

        Ok(())
    }
}

#[pin_data]
struct SampleModule {
    #[pin]
    _pci_driver: driver::Registration<pci::Adapter<ParentDriver>>,
    #[pin]
    _aux_driver: driver::Registration<auxiliary::Adapter<AuxiliaryDriver>>,
}

impl InPlaceModule for SampleModule {
    fn init(module: &'static kernel::ThisModule) -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            _pci_driver <- driver::Registration::new(MODULE_NAME, module),
            _aux_driver <- driver::Registration::new(MODULE_NAME, module),
        })
    }
}

module! {
    type: SampleModule,
    name: "rust_driver_auxiliary",
    authors: ["Danilo Krummrich"],
    description: "Rust auxiliary driver",
    license: "GPL v2",
}

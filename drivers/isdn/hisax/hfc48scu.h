// $Id: hfc48scu.h,v 1.1 2004/03/11 16:11:51 martinb1 Exp $
// $Revision: 1.1 $
//___________________________________________________________________________________//
//                                                                                   //
//  (C) Copyright Cologne Chip AG, 2003                                              //
//___________________________________________________________________________________//
//                                                                                   //
//  File name:     HFC48Scu.h                                                        //
//  File content:  This file contains the HFC-4S and HFC-8S register definitions.    //
//  Creation date: 03rd of February 2003                                             //
//  Creator:       GENERO V1.3                                                       //
//  Data base:     HFC-XML V1.4                                                      //
//  File Revision: 1.1                                                               //
//                                                                                   //
//  The information presented can not be considered as assured characteristics.      //
//  Data can change without notice. Please check version numbers in case of doubt.   //
//                                                                                   //
//  For further information or questions please contact                              //
//  support@CologneChip.com                                                          //
//                                                                                   //
//  See below for examples how to use this file.                                     //
//                                                                                   //
//                                                                                   //
//                                                                                   //
//           _____________________________________________________________           //
//                                                                                   //
//             This file has beta status. It is still under investigation.           //
//             We have tried to fulfil common user requirements.                     //
//                                                                                   //
//             We'd be pleased about feedback for any suggestions to                 //
//             improve this header file for our customer's use.                      //
//           _____________________________________________________________           //
//                                                                                   //
//                                                                                   //
//                                                                                   //
//___________________________________________________________________________________//
//                                                                                   //
//  WARNING: This file has been generated automatically and should not be            //
//           changed to maintain compatibility with later versions.                  //
//___________________________________________________________________________________//
//                                                                                   //

#ifndef HFC_4S_8S_cu
#define HFC_4S_8S_cu


typedef unsigned char  BYTE;
typedef BYTE REGWORD;             // chip dependend maximum register length
typedef unsigned short REGWORD16; // chip dependend maximum register length
typedef unsigned int REGWORD32;   // chip dependend maximum register length




//___________________________________________________________________________________//
//                                                                                   //
//  HFC48Scu.h usage:                                                                //
//___________________________________________________________________________________//
//                                                                                   //
// How can this header file be used? The main idea is, to have names not only for    //
// registers but also for every bitmap. The concept allows Bitmap access without     //
// shifting the values to their real position.                                       //
//                                                                                   //
// Every bitmap V_.. has a mask named M_.. where all mask bits are set.              //
// If a bitmap has a length of more than 1 bit but less than the register width,     //
// there are supplementary mask-values for every valid value, i.e. M1_.., M2_..,     //
// .., M<max>_..                                                                     //
//                                                                                   //
// In the following examples a procedure                                             //
//    writereg(BYTE port, REGWORD val)  // writes val into port                      //
//                                                                                   //
// is used. This must be implemented by the user.                                    //
//                                                                                   //
// For all examples the register A_CONF has been choosen.                            //
//                                                                                   //
// 1. Approach: access without variable                                              //
// ------------------------------------                                              //
//                                                                                   //
//     writereg(A_CONF, M3_CONF_NUM | M_CONF_EN); // initialisation: selected PCM    //
//                                                // time slot added to conference   //
//                                                // #3 without noise suppr. and     //
//                                                // 0 dB atten. level               //
//                                                                                   //
//     // ...                                                                        //
//                                                                                   //
//     a_conf.reg = M3_CONF_NUM | M_CONF_EN | M1_ATT_LEV | M3_NOISE_SUPPR;           //
//                                                // changing the settings: -3 dB    //
//                                                // atten. level and strong noise   //
//                                                // suppression                     //
//                                                                                   //
// When calculating bitmap values, please note, that incrementation / decrementation //
// must be done with m1_.. value! One must take care that a bitmap value is always   //
// in the range 0 <= v_.. <= M_..                                                    //
//                                                                                   //
// This 1st approach has the advantage that it needs no variable. But if read-back   //
// functionality is required the next technique should be used.                      //
//                                                                                   //
// 2. Approach: access with read-back functionallity                                 //
// -------------------------------------------------                                 //
//                                                                                   //
//     reg_a_conf a_conf; // declaration of chip variable                            //
//                                                                                   //
//     a_conf.bit.v_conf_num = 3;    // initialization,                              //
//     a_conf.bit.v_noise_suppr = 0; // same values as above                         //
//     a_conf.bit.v_att_lev = 0;     //                                              //
//     a_conf.bit.v_conf_en = 1;     //                                              //
//                                                                                   //
//     writereg(A_CONF, a_conf.reg); // value transfer into the register             //
//                                                                                   //
//     Now it is possible to change one or more bitmaps:                             //
//                                                                                   //
//     a_conf.bit.v_noise_suppr = m_att_lev; // strongest noise suppr. (same as      //
//                                           // m3_att_lev)                          //
//     a_conf.bit.v_att_lev = m1_att_lev; // -3 dB atten. level                      //
//                                                                                   //
//     a_conf.reg = a_conf.reg; // value transfer into the register                  //
//                                                                                   //
//___________________________________________________________________________________//



///////////////////////////////////////////////////////////////////////////////////////
// common data definition
///////////////////////////////////////////////////////////////////////////////////////

    // chip information:
    #define CHIP_NAME_4S "HFC-4S"
    #define CHIP_NAME_8S "HFC-8S"
    #define CHIP_TITLE_4S "ISDN HDLC FIFO controller with 4 S/T interfaces"
    #define CHIP_TITLE_8S "ISDN HDLC FIFO controller with 8 S/T interfaces"
    #define CHIP_MANUFACTURER "CologneChip"
    #define CHIP_ID_4S 0x0C
    #define CHIP_ID_8S 0x08
    #define CHIP_ID_SHIFT 4
    #define CHIP_REGISTER_COUNT 124
    #define CHIP_DATABASE ""

    // PCI configuration:
    #define PCI_VENDOR_ID_CCD 0x1397
    #define PCI_DEVICE_ID_4S 0x08B4
    #define PCI_DEVICE_ID_8S 0x16B8
    #define PCI_REVISION_ID_4S 0x01
    #define PCI_REVISION_ID_8S 0x01


///////////////////////////////////////////////////////////////////////////////////////
// begin of register list
///////////////////////////////////////////////////////////////////////////////////////


#define R_IRQ_STATECH 0x12

#define M_STATECH_ST0 0x01
#define M_STATECH_ST1 0x02
#define M_STATECH_ST2 0x04
#define M_STATECH_ST3 0x08
#define M_STATECH_ST4 0x10
#define M_STATECH_ST5 0x20
#define M_STATECH_ST6 0x40
#define M_STATECH_ST7 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_statech_st0:1;
        REGWORD v_statech_st1:1;
        REGWORD v_statech_st2:1;
        REGWORD v_statech_st3:1;
        REGWORD v_statech_st4:1;
        REGWORD v_statech_st5:1;
        REGWORD v_statech_st6:1;
        REGWORD v_statech_st7:1;
    } bit_r_irq_statech;

    typedef union {REGWORD reg; bit_r_irq_statech bit;} reg_r_irq_statech; // register and bitmap access

#define R_IRQMSK_STATCHG 0x12

#define M_IRQMSK_STACHG_ST0 0x01
#define M_IRQMSK_STACHG_ST1 0x02
#define M_IRQMSK_STACHG_ST2 0x04
#define M_IRQMSK_STACHG_ST3 0x08
#define M_IRQMSK_STACHG_ST4 0x10
#define M_IRQMSK_STACHG_ST5 0x20
#define M_IRQMSK_STACHG_ST6 0x40
#define M_IRQMSK_STACHG_ST7 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_irqmsk_stachg_st0:1;
        REGWORD v_irqmsk_stachg_st1:1;
        REGWORD v_irqmsk_stachg_st2:1;
        REGWORD v_irqmsk_stachg_st3:1;
        REGWORD v_irqmsk_stachg_st4:1;
        REGWORD v_irqmsk_stachg_st5:1;
        REGWORD v_irqmsk_stachg_st6:1;
        REGWORD v_irqmsk_stachg_st7:1;
    } bit_r_irqmsk_statchg;

    typedef union {REGWORD reg; bit_r_irqmsk_statchg bit;} reg_r_irqmsk_statchg; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_ST_SEL 0x16

#define M_ST_SEL 0x07
  #define M1_ST_SEL 0x01
  #define M2_ST_SEL 0x02
  #define M3_ST_SEL 0x03
  #define M4_ST_SEL 0x04
  #define M5_ST_SEL 0x05
  #define M6_ST_SEL 0x06
  #define M7_ST_SEL 0x07
#define M_MULT_ST 0x08

    typedef struct // bitmap construction
    {
        REGWORD v_st_sel:3;
        REGWORD v_mult_st:1;
        REGWORD reserved_0:4;
    } bit_r_st_sel;

    typedef union {REGWORD reg; bit_r_st_sel bit;} reg_r_st_sel; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_ST_SYNC 0x17

#define M_SYNC_SEL 0x07
  #define M1_SYNC_SEL 0x01
  #define M2_SYNC_SEL 0x02
  #define M3_SYNC_SEL 0x03
  #define M4_SYNC_SEL 0x04
  #define M5_SYNC_SEL 0x05
  #define M6_SYNC_SEL 0x06
  #define M7_SYNC_SEL 0x07
#define M_AUTO_SYNC 0x08

    typedef struct // bitmap construction
    {
        REGWORD v_sync_sel:3;
        REGWORD v_auto_sync:1;
        REGWORD reserved_1:4;
    } bit_r_st_sync;

    typedef union {REGWORD reg; bit_r_st_sync bit;} reg_r_st_sync; // register and bitmap access

#define A_ST_RD_STA 0x30

#define M_ST_STA 0x0F
  #define M1_ST_STA 0x01
  #define M2_ST_STA 0x02
  #define M3_ST_STA 0x03
  #define M4_ST_STA 0x04
  #define M5_ST_STA 0x05
  #define M6_ST_STA 0x06
  #define M7_ST_STA 0x07
  #define M8_ST_STA 0x08
  #define M9_ST_STA 0x09
  #define M10_ST_STA 0x0A
  #define M11_ST_STA 0x0B
  #define M12_ST_STA 0x0C
  #define M13_ST_STA 0x0D
  #define M14_ST_STA 0x0E
  #define M15_ST_STA 0x0F
#define M_FR_SYNC 0x10
#define M_TI2_EXP 0x20
#define M_INFO0 0x40
#define M_G2_G3 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_st_sta:4;
        REGWORD v_fr_sync:1;
        REGWORD v_ti2_exp:1;
        REGWORD v_info0:1;
        REGWORD v_g2_g3:1;
    } bit_a_st_rd_sta;

    typedef union {REGWORD reg; bit_a_st_rd_sta bit;} reg_a_st_rd_sta; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_ST_SQ_RD 0x34

#define M_ST_SQ 0x0F
  #define M1_ST_SQ 0x01
  #define M2_ST_SQ 0x02
  #define M3_ST_SQ 0x03
  #define M4_ST_SQ 0x04
  #define M5_ST_SQ 0x05
  #define M6_ST_SQ 0x06
  #define M7_ST_SQ 0x07
  #define M8_ST_SQ 0x08
  #define M9_ST_SQ 0x09
  #define M10_ST_SQ 0x0A
  #define M11_ST_SQ 0x0B
  #define M12_ST_SQ 0x0C
  #define M13_ST_SQ 0x0D
  #define M14_ST_SQ 0x0E
  #define M15_ST_SQ 0x0F
#define M_MF_RX_RDY 0x10
#define M_MF_TX_RDY 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_st_sq:4;
        REGWORD v_mf_rx_rdy:1;
        REGWORD reserved_2:2;
        REGWORD v_mf_tx_rdy:1;
    } bit_a_st_sq_rd;

    typedef union {REGWORD reg; bit_a_st_sq_rd bit;} reg_a_st_sq_rd; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_ST_B1_RX 0x3C

#define M_ST_B1_RX 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_st_b1_rx:8;
    } bit_a_st_b1_rx;

    typedef union {REGWORD reg; bit_a_st_b1_rx bit;} reg_a_st_b1_rx; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_ST_B2_RX 0x3D

#define M_ST_B2_RX 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_st_b2_rx:8;
    } bit_a_st_b2_rx;

    typedef union {REGWORD reg; bit_a_st_b2_rx bit;} reg_a_st_b2_rx; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_ST_D_RX 0x3E

#define M_ST_D_RX 0xC0
  #define M1_ST_D_RX 0x40
  #define M2_ST_D_RX 0x80
  #define M3_ST_D_RX 0xC0

    typedef struct // bitmap construction
    {
        REGWORD reserved_3:6;
        REGWORD v_st_d_rx:2;
    } bit_a_st_d_rx;

    typedef union {REGWORD reg; bit_a_st_d_rx bit;} reg_a_st_d_rx; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_ST_E_RX 0x3F

#define M_ST_E_RX 0xC0
  #define M1_ST_E_RX 0x40
  #define M2_ST_E_RX 0x80
  #define M3_ST_E_RX 0xC0

    typedef struct // bitmap construction
    {
        REGWORD reserved_4:6;
        REGWORD v_st_e_rx:2;
    } bit_a_st_e_rx;

    typedef union {REGWORD reg; bit_a_st_e_rx bit;} reg_a_st_e_rx; // register and bitmap access

#define A_ST_WR_STA 0x30

#define M_ST_SET_STA 0x0F
  #define M1_ST_SET_STA 0x01
  #define M2_ST_SET_STA 0x02
  #define M3_ST_SET_STA 0x03
  #define M4_ST_SET_STA 0x04
  #define M5_ST_SET_STA 0x05
  #define M6_ST_SET_STA 0x06
  #define M7_ST_SET_STA 0x07
  #define M8_ST_SET_STA 0x08
  #define M9_ST_SET_STA 0x09
  #define M10_ST_SET_STA 0x0A
  #define M11_ST_SET_STA 0x0B
  #define M12_ST_SET_STA 0x0C
  #define M13_ST_SET_STA 0x0D
  #define M14_ST_SET_STA 0x0E
  #define M15_ST_SET_STA 0x0F
#define M_ST_LD_STA 0x10
#define M_ST_ACT 0x60
  #define M1_ST_ACT 0x20
  #define M2_ST_ACT 0x40
  #define M3_ST_ACT 0x60
#define M_SET_G2_G3 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_st_set_sta:4;
        REGWORD v_st_ld_sta:1;
        REGWORD v_st_act:2;
        REGWORD v_set_g2_g3:1;
    } bit_a_st_wr_sta;

    typedef union {REGWORD reg; bit_a_st_wr_sta bit;} reg_a_st_wr_sta; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_ST_CTRL0 0x31

#define M_B1_EN 0x01
#define M_B2_EN 0x02
#define M_ST_MD 0x04
#define M_D_PRIO 0x08
#define M_SQ_EN 0x10
#define M_96KHZ 0x20
#define M_TX_LI 0x40
#define M_ST_STOP 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_b1_en:1;
        REGWORD v_b2_en:1;
        REGWORD v_st_md:1;
        REGWORD v_d_prio:1;
        REGWORD v_sq_en:1;
        REGWORD v_96khz:1;
        REGWORD v_tx_li:1;
        REGWORD v_st_stop:1;
    } bit_a_st_ctrl0;

    typedef union {REGWORD reg; bit_a_st_ctrl0 bit;} reg_a_st_ctrl0; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_ST_CTRL1 0x32

#define M_G2_G3_EN 0x01
#define M_D_HI 0x04
#define M_E_IGNO 0x08
#define M_E_LO 0x10
#define M_B12_SWAP 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_g2_g3_en:1;
        REGWORD reserved_5:1;
        REGWORD v_d_hi:1;
        REGWORD v_e_igno:1;
        REGWORD v_e_lo:1;
        REGWORD reserved_6:2;
        REGWORD v_b12_swap:1;
    } bit_a_st_ctrl1;

    typedef union {REGWORD reg; bit_a_st_ctrl1 bit;} reg_a_st_ctrl1; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_ST_CTRL2 0x33

#define M_B1_RX_EN 0x01
#define M_B2_RX_EN 0x02
#define M_ST_TRIS 0x40

    typedef struct // bitmap construction
    {
        REGWORD v_b1_rx_en:1;
        REGWORD v_b2_rx_en:1;
        REGWORD reserved_7:4;
        REGWORD v_st_tris:1;
        REGWORD reserved_8:1;
    } bit_a_st_ctrl2;

    typedef union {REGWORD reg; bit_a_st_ctrl2 bit;} reg_a_st_ctrl2; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_ST_SQ_WR 0x34

#define M_ST_SQ 0x0F
  #define M1_ST_SQ 0x01
  #define M2_ST_SQ 0x02
  #define M3_ST_SQ 0x03
  #define M4_ST_SQ 0x04
  #define M5_ST_SQ 0x05
  #define M6_ST_SQ 0x06
  #define M7_ST_SQ 0x07
  #define M8_ST_SQ 0x08
  #define M9_ST_SQ 0x09
  #define M10_ST_SQ 0x0A
  #define M11_ST_SQ 0x0B
  #define M12_ST_SQ 0x0C
  #define M13_ST_SQ 0x0D
  #define M14_ST_SQ 0x0E
  #define M15_ST_SQ 0x0F

    typedef struct // bitmap construction
    {
        REGWORD v_st_sq:4;
        REGWORD reserved_9:4;
    } bit_a_st_sq_wr;

    typedef union {REGWORD reg; bit_a_st_sq_wr bit;} reg_a_st_sq_wr; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_ST_CLK_DLY 0x37

#define M_ST_CLK_DLY 0x0F
  #define M1_ST_CLK_DLY 0x01
  #define M2_ST_CLK_DLY 0x02
  #define M3_ST_CLK_DLY 0x03
  #define M4_ST_CLK_DLY 0x04
  #define M5_ST_CLK_DLY 0x05
  #define M6_ST_CLK_DLY 0x06
  #define M7_ST_CLK_DLY 0x07
  #define M8_ST_CLK_DLY 0x08
  #define M9_ST_CLK_DLY 0x09
  #define M10_ST_CLK_DLY 0x0A
  #define M11_ST_CLK_DLY 0x0B
  #define M12_ST_CLK_DLY 0x0C
  #define M13_ST_CLK_DLY 0x0D
  #define M14_ST_CLK_DLY 0x0E
  #define M15_ST_CLK_DLY 0x0F
#define M_ST_SMPL 0x70
  #define M1_ST_SMPL 0x10
  #define M2_ST_SMPL 0x20
  #define M3_ST_SMPL 0x30
  #define M4_ST_SMPL 0x40
  #define M5_ST_SMPL 0x50
  #define M6_ST_SMPL 0x60
  #define M7_ST_SMPL 0x70

    typedef struct // bitmap construction
    {
        REGWORD v_st_clk_dly:4;
        REGWORD v_st_smpl:3;
        REGWORD reserved_10:1;
    } bit_a_st_clk_dly;

    typedef union {REGWORD reg; bit_a_st_clk_dly bit;} reg_a_st_clk_dly; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_ST_B1_TX 0x3C

#define M_ST_B1_TX 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_st_b1_tx:8;
    } bit_a_st_b1_tx;

    typedef union {REGWORD reg; bit_a_st_b1_tx bit;} reg_a_st_b1_tx; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_ST_B2_TX 0x3D

#define M_ST_B2_TX 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_st_b2_tx:8;
    } bit_a_st_b2_tx;

    typedef union {REGWORD reg; bit_a_st_b2_tx bit;} reg_a_st_b2_tx; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_ST_D_TX 0x3E

#define M_ST_D_TX 0xC0
  #define M1_ST_D_TX 0x40
  #define M2_ST_D_TX 0x80
  #define M3_ST_D_TX 0xC0

    typedef struct // bitmap construction
    {
        REGWORD reserved_11:6;
        REGWORD v_st_d_tx:2;
    } bit_a_st_d_tx;

    typedef union {REGWORD reg; bit_a_st_d_tx bit;} reg_a_st_d_tx; // register and bitmap access

#define A_CHANNEL 0xFC

#define M_CH_DIR 0x01
#define M_CH0_SEL 0x3E
  #define M1_CH0_SEL 0x02
  #define M2_CH0_SEL 0x04
  #define M3_CH0_SEL 0x06
  #define M4_CH0_SEL 0x08
  #define M5_CH0_SEL 0x0A
  #define M6_CH0_SEL 0x0C
  #define M7_CH0_SEL 0x0E
  #define M8_CH0_SEL 0x10
  #define M9_CH0_SEL 0x12
  #define M10_CH0_SEL 0x14
  #define M11_CH0_SEL 0x16
  #define M12_CH0_SEL 0x18
  #define M13_CH0_SEL 0x1A
  #define M14_CH0_SEL 0x1C
  #define M15_CH0_SEL 0x1E
  #define M16_CH0_SEL 0x20
  #define M17_CH0_SEL 0x22
  #define M18_CH0_SEL 0x24
  #define M19_CH0_SEL 0x26
  #define M20_CH0_SEL 0x28
  #define M21_CH0_SEL 0x2A
  #define M22_CH0_SEL 0x2C
  #define M23_CH0_SEL 0x2E
  #define M24_CH0_SEL 0x30
  #define M25_CH0_SEL 0x32
  #define M26_CH0_SEL 0x34
  #define M27_CH0_SEL 0x36
  #define M28_CH0_SEL 0x38
  #define M29_CH0_SEL 0x3A
  #define M30_CH0_SEL 0x3C
  #define M31_CH0_SEL 0x3E

    typedef struct // bitmap construction
    {
        REGWORD v_ch_dir:1;
        REGWORD v_ch0_sel:5;
        REGWORD reserved_12:2;
    } bit_a_channel;

    typedef union {REGWORD reg; bit_a_channel bit;} reg_a_channel; // register and bitmap access

#define A_Z1L 0x04

#define M_Z1L 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_z1l:8;
    } bit_a_z1l;

    typedef union {REGWORD reg; bit_a_z1l bit;} reg_a_z1l; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_Z1H 0x05

#define M_Z1H 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_z1h:8;
    } bit_a_z1h;

    typedef union {REGWORD reg; bit_a_z1h bit;} reg_a_z1h; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_Z1 0x04

#define M_Z1 0xFFFF

    typedef struct // bitmap construction
    {
      REGWORD16 v_z1:16;
    } bit_a_z1;

    typedef union {REGWORD reg; bit_a_z1 bit;} reg_a_z1; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_Z2L 0x06

#define M_Z2L 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_z2l:8;
    } bit_a_z2l;

    typedef union {REGWORD reg; bit_a_z2l bit;} reg_a_z2l; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_Z2H 0x07

#define M_Z2H 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_z2h:8;
    } bit_a_z2h;

    typedef union {REGWORD reg; bit_a_z2h bit;} reg_a_z2h; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_Z2 0x06

#define M_Z2 0xFFFF

    typedef struct // bitmap construction
    {
      REGWORD16 v_z2:16;
    } bit_a_z2;

    typedef union {REGWORD reg; bit_a_z2 bit;} reg_a_z2; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_Z12 0x04

#define M_Z12 0xFFFFFFFF

    #ifdef COMPILER_32BIT // can be defined by user to allow 32 bit compiler mode
    typedef struct // bitmap construction
    {
      REGWORD32 v_z12:32;
    } bit_a_z12;

    typedef union {REGWORD reg; bit_a_z12 bit;} reg_a_z12; // register and bitmap access
    #endif

//___________________________________________________________________________________//
//                                                                                   //
#define A_F1 0x0C

#define M_F1 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_f1:8;
    } bit_a_f1;

    typedef union {REGWORD reg; bit_a_f1 bit;} reg_a_f1; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_F2 0x0D

#define M_F2 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_f2:8;
    } bit_a_f2;

    typedef union {REGWORD reg; bit_a_f2 bit;} reg_a_f2; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_F12 0x0C

#define M_F12 0xFFFF

    typedef struct // bitmap construction
    {
      REGWORD16 v_f12:16;
    } bit_a_f12;

    typedef union {REGWORD reg; bit_a_f12 bit;} reg_a_f12; // register and bitmap access

#define R_CIRM 0x00

#define M_IRQ_SEL 0x07
  #define M1_IRQ_SEL 0x01
  #define M2_IRQ_SEL 0x02
  #define M3_IRQ_SEL 0x03
  #define M4_IRQ_SEL 0x04
  #define M5_IRQ_SEL 0x05
  #define M6_IRQ_SEL 0x06
  #define M7_IRQ_SEL 0x07
#define M_SRES 0x08
#define M_HFCRES 0x10
#define M_PCMRES 0x20
#define M_STRES 0x40
#define M_RLD_EPR 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_irq_sel:3;
        REGWORD v_sres:1;
        REGWORD v_hfcres:1;
        REGWORD v_pcmres:1;
        REGWORD v_stres:1;
        REGWORD v_rld_epr:1;
    } bit_r_cirm;

    typedef union {REGWORD reg; bit_r_cirm bit;} reg_r_cirm; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_CTRL 0x01

#define M_FIFO_LPRIO 0x02
#define M_SLOW_RD 0x04
#define M_EXT_RAM 0x08
#define M_CLK_OFF 0x20

    typedef struct // bitmap construction
    {
        REGWORD reserved_13:1;
        REGWORD v_fifo_lprio:1;
        REGWORD v_slow_rd:1;
        REGWORD v_ext_ram:1;
        REGWORD reserved_14:1;
        REGWORD v_clk_off:1;
        REGWORD reserved_15v_st_clk:2;
    } bit_r_ctrl;

    typedef union {REGWORD reg; bit_r_ctrl bit;} reg_r_ctrl; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BRG_PCM_CFG 0x02

#define M_BRG_EN 0x01
#define M_BRG_MD 0x02
#define M_PCM_CLK 0x20

    typedef struct // bitmap construction
    {
        REGWORD v_brg_en:1;
        REGWORD v_brg_md:1;
        REGWORD reserved_16:3;
        REGWORD v_pcm_clk:1;
        REGWORD reserved_17v_addr_wrdly:2;
    } bit_r_brg_pcm_cfg;

    typedef union {REGWORD reg; bit_r_brg_pcm_cfg bit;} reg_r_brg_pcm_cfg; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_RAM_ADDR0 0x08

#define M_RAM_ADDR0 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_ram_addr0:8;
    } bit_r_ram_addr0;

    typedef union {REGWORD reg; bit_r_ram_addr0 bit;} reg_r_ram_addr0; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_RAM_ADDR1 0x09

#define M_RAM_ADDR1 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_ram_addr1:8;
    } bit_r_ram_addr1;

    typedef union {REGWORD reg; bit_r_ram_addr1 bit;} reg_r_ram_addr1; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_RAM_ADDR2 0x0A

#define M_RAM_ADDR2 0x0F
  #define M1_RAM_ADDR2 0x01
  #define M2_RAM_ADDR2 0x02
  #define M3_RAM_ADDR2 0x03
  #define M4_RAM_ADDR2 0x04
  #define M5_RAM_ADDR2 0x05
  #define M6_RAM_ADDR2 0x06
  #define M7_RAM_ADDR2 0x07
  #define M8_RAM_ADDR2 0x08
  #define M9_RAM_ADDR2 0x09
  #define M10_RAM_ADDR2 0x0A
  #define M11_RAM_ADDR2 0x0B
  #define M12_RAM_ADDR2 0x0C
  #define M13_RAM_ADDR2 0x0D
  #define M14_RAM_ADDR2 0x0E
  #define M15_RAM_ADDR2 0x0F
#define M_ADDR_RES 0x40
#define M_ADDR_INC 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_ram_addr2:4;
        REGWORD reserved_18:2;
        REGWORD v_addr_res:1;
        REGWORD v_addr_inc:1;
    } bit_r_ram_addr2;

    typedef union {REGWORD reg; bit_r_ram_addr2 bit;} reg_r_ram_addr2; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_FIRST_FIFO 0x0B

#define M_FIRST_FIFO_DIR 0x01
#define M_FIRST_FIFO_NUM 0x3E
  #define M1_FIRST_FIFO_NUM 0x02
  #define M2_FIRST_FIFO_NUM 0x04
  #define M3_FIRST_FIFO_NUM 0x06
  #define M4_FIRST_FIFO_NUM 0x08
  #define M5_FIRST_FIFO_NUM 0x0A
  #define M6_FIRST_FIFO_NUM 0x0C
  #define M7_FIRST_FIFO_NUM 0x0E
  #define M8_FIRST_FIFO_NUM 0x10
  #define M9_FIRST_FIFO_NUM 0x12
  #define M10_FIRST_FIFO_NUM 0x14
  #define M11_FIRST_FIFO_NUM 0x16
  #define M12_FIRST_FIFO_NUM 0x18
  #define M13_FIRST_FIFO_NUM 0x1A
  #define M14_FIRST_FIFO_NUM 0x1C
  #define M15_FIRST_FIFO_NUM 0x1E
  #define M16_FIRST_FIFO_NUM 0x20
  #define M17_FIRST_FIFO_NUM 0x22
  #define M18_FIRST_FIFO_NUM 0x24
  #define M19_FIRST_FIFO_NUM 0x26
  #define M20_FIRST_FIFO_NUM 0x28
  #define M21_FIRST_FIFO_NUM 0x2A
  #define M22_FIRST_FIFO_NUM 0x2C
  #define M23_FIRST_FIFO_NUM 0x2E
  #define M24_FIRST_FIFO_NUM 0x30
  #define M25_FIRST_FIFO_NUM 0x32
  #define M26_FIRST_FIFO_NUM 0x34
  #define M27_FIRST_FIFO_NUM 0x36
  #define M28_FIRST_FIFO_NUM 0x38
  #define M29_FIRST_FIFO_NUM 0x3A
  #define M30_FIRST_FIFO_NUM 0x3C
  #define M31_FIRST_FIFO_NUM 0x3E

    typedef struct // bitmap construction
    {
        REGWORD v_first_fifo_dir:1;
        REGWORD v_first_fifo_num:5;
        REGWORD reserved_19:2;
    } bit_r_first_fifo;

    typedef union {REGWORD reg; bit_r_first_fifo bit;} reg_r_first_fifo; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_RAM_SZ 0x0C

#define M_RAM_SZ 0x03
  #define M1_RAM_SZ 0x01
  #define M2_RAM_SZ 0x02
  #define M3_RAM_SZ 0x03
  #define V_FZ_MD   0x80

    typedef struct // bitmap construction
    {
        REGWORD v_ram_sz:2;
        REGWORD reserved_20:6;
    } bit_r_ram_sz;

    typedef union {REGWORD reg; bit_r_ram_sz bit;} reg_r_ram_sz; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_FIFO_MD 0x0D

#define M_FIFO_MD 0x03
  #define M1_FIFO_MD 0x01
  #define M2_FIFO_MD 0x02
  #define M3_FIFO_MD 0x03
#define M_CSM_MD 0x04
#define M_FSM_MD 0x08
#define M_FIFO_SZ 0x30
  #define M1_FIFO_SZ 0x10
  #define M2_FIFO_SZ 0x20
  #define M3_FIFO_SZ 0x30

    typedef struct // bitmap construction
    {
        REGWORD v_fifo_md:2;
        REGWORD v_csm_md:1;
        REGWORD v_fsm_md:1;
        REGWORD v_fifo_sz:2;
        REGWORD reserved_21:2;
    } bit_r_fifo_md;

    typedef union {REGWORD reg; bit_r_fifo_md bit;} reg_r_fifo_md; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_INC_RES_FIFO 0x0E

#define M_INC_F 0x01
#define M_RES_F 0x02
#define M_RES_LOST 0x04

    typedef struct // bitmap construction
    {
        REGWORD v_inc_f:1;
        REGWORD v_res_f:1;
        REGWORD v_res_lost:1;
        REGWORD reserved_22:5;
    } bit_r_inc_res_fifo;

    typedef union {REGWORD reg; bit_r_inc_res_fifo bit;} reg_r_inc_res_fifo; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_FIFO 0x0F

#define M_FIFO_DIR 0x01
#define M_FIFO_NUM 0x3E
  #define M1_FIFO_NUM 0x02
  #define M2_FIFO_NUM 0x04
  #define M3_FIFO_NUM 0x06
  #define M4_FIFO_NUM 0x08
  #define M5_FIFO_NUM 0x0A
  #define M6_FIFO_NUM 0x0C
  #define M7_FIFO_NUM 0x0E
  #define M8_FIFO_NUM 0x10
  #define M9_FIFO_NUM 0x12
  #define M10_FIFO_NUM 0x14
  #define M11_FIFO_NUM 0x16
  #define M12_FIFO_NUM 0x18
  #define M13_FIFO_NUM 0x1A
  #define M14_FIFO_NUM 0x1C
  #define M15_FIFO_NUM 0x1E
  #define M16_FIFO_NUM 0x20
  #define M17_FIFO_NUM 0x22
  #define M18_FIFO_NUM 0x24
  #define M19_FIFO_NUM 0x26
  #define M20_FIFO_NUM 0x28
  #define M21_FIFO_NUM 0x2A
  #define M22_FIFO_NUM 0x2C
  #define M23_FIFO_NUM 0x2E
  #define M24_FIFO_NUM 0x30
  #define M25_FIFO_NUM 0x32
  #define M26_FIFO_NUM 0x34
  #define M27_FIFO_NUM 0x36
  #define M28_FIFO_NUM 0x38
  #define M29_FIFO_NUM 0x3A
  #define M30_FIFO_NUM 0x3C
  #define M31_FIFO_NUM 0x3E
#define M_REV 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_fifo_dir:1;
        REGWORD v_fifo_num:5;
        REGWORD reserved_23:1;
        REGWORD v_rev:1;
    } bit_r_fifo;

    typedef union {REGWORD reg; bit_r_fifo bit;} reg_r_fifo; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_FSM_IDX 0x0F

#define M_IDX 0x3F
  #define M1_IDX 0x01
  #define M2_IDX 0x02
  #define M3_IDX 0x03
  #define M4_IDX 0x04
  #define M5_IDX 0x05
  #define M6_IDX 0x06
  #define M7_IDX 0x07
  #define M8_IDX 0x08
  #define M9_IDX 0x09
  #define M10_IDX 0x0A
  #define M11_IDX 0x0B
  #define M12_IDX 0x0C
  #define M13_IDX 0x0D
  #define M14_IDX 0x0E
  #define M15_IDX 0x0F
  #define M16_IDX 0x10
  #define M17_IDX 0x11
  #define M18_IDX 0x12
  #define M19_IDX 0x13
  #define M20_IDX 0x14
  #define M21_IDX 0x15
  #define M22_IDX 0x16
  #define M23_IDX 0x17
  #define M24_IDX 0x18
  #define M25_IDX 0x19
  #define M26_IDX 0x1A
  #define M27_IDX 0x1B
  #define M28_IDX 0x1C
  #define M29_IDX 0x1D
  #define M30_IDX 0x1E
  #define M31_IDX 0x1F
  #define M32_IDX 0x20
  #define M33_IDX 0x21
  #define M34_IDX 0x22
  #define M35_IDX 0x23
  #define M36_IDX 0x24
  #define M37_IDX 0x25
  #define M38_IDX 0x26
  #define M39_IDX 0x27
  #define M40_IDX 0x28
  #define M41_IDX 0x29
  #define M42_IDX 0x2A
  #define M43_IDX 0x2B
  #define M44_IDX 0x2C
  #define M45_IDX 0x2D
  #define M46_IDX 0x2E
  #define M47_IDX 0x2F
  #define M48_IDX 0x30
  #define M49_IDX 0x31
  #define M50_IDX 0x32
  #define M51_IDX 0x33
  #define M52_IDX 0x34
  #define M53_IDX 0x35
  #define M54_IDX 0x36
  #define M55_IDX 0x37
  #define M56_IDX 0x38
  #define M57_IDX 0x39
  #define M58_IDX 0x3A
  #define M59_IDX 0x3B
  #define M60_IDX 0x3C
  #define M61_IDX 0x3D
  #define M62_IDX 0x3E
  #define M63_IDX 0x3F

    typedef struct // bitmap construction
    {
        REGWORD v_idx:6;
        REGWORD reserved_24:2;
    } bit_r_fsm_idx;

    typedef union {REGWORD reg; bit_r_fsm_idx bit;} reg_r_fsm_idx; // register and bitmap access

#define R_SRAM_USE 0x15

#define M_SRAM_USE 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_sram_use:8;
    } bit_r_sram_use;

    typedef union {REGWORD reg; bit_r_sram_use bit;} reg_r_sram_use; // register and bitmap access

#define R_SL_SEL0 0x15

#define M_SL_SEL0 0x7F
  #define M1_SL_SEL0 0x01
  #define M2_SL_SEL0 0x02
  #define M3_SL_SEL0 0x03
  #define M4_SL_SEL0 0x04
  #define M5_SL_SEL0 0x05
  #define M6_SL_SEL0 0x06
  #define M7_SL_SEL0 0x07
  #define M8_SL_SEL0 0x08
  #define M9_SL_SEL0 0x09
  #define M10_SL_SEL0 0x0A
  #define M11_SL_SEL0 0x0B
  #define M12_SL_SEL0 0x0C
  #define M13_SL_SEL0 0x0D
  #define M14_SL_SEL0 0x0E
  #define M15_SL_SEL0 0x0F
  #define M16_SL_SEL0 0x10
  #define M17_SL_SEL0 0x11
  #define M18_SL_SEL0 0x12
  #define M19_SL_SEL0 0x13
  #define M20_SL_SEL0 0x14
  #define M21_SL_SEL0 0x15
  #define M22_SL_SEL0 0x16
  #define M23_SL_SEL0 0x17
  #define M24_SL_SEL0 0x18
  #define M25_SL_SEL0 0x19
  #define M26_SL_SEL0 0x1A
  #define M27_SL_SEL0 0x1B
  #define M28_SL_SEL0 0x1C
  #define M29_SL_SEL0 0x1D
  #define M30_SL_SEL0 0x1E
  #define M31_SL_SEL0 0x1F
  #define M32_SL_SEL0 0x20
  #define M33_SL_SEL0 0x21
  #define M34_SL_SEL0 0x22
  #define M35_SL_SEL0 0x23
  #define M36_SL_SEL0 0x24
  #define M37_SL_SEL0 0x25
  #define M38_SL_SEL0 0x26
  #define M39_SL_SEL0 0x27
  #define M40_SL_SEL0 0x28
  #define M41_SL_SEL0 0x29
  #define M42_SL_SEL0 0x2A
  #define M43_SL_SEL0 0x2B
  #define M44_SL_SEL0 0x2C
  #define M45_SL_SEL0 0x2D
  #define M46_SL_SEL0 0x2E
  #define M47_SL_SEL0 0x2F
  #define M48_SL_SEL0 0x30
  #define M49_SL_SEL0 0x31
  #define M50_SL_SEL0 0x32
  #define M51_SL_SEL0 0x33
  #define M52_SL_SEL0 0x34
  #define M53_SL_SEL0 0x35
  #define M54_SL_SEL0 0x36
  #define M55_SL_SEL0 0x37
  #define M56_SL_SEL0 0x38
  #define M57_SL_SEL0 0x39
  #define M58_SL_SEL0 0x3A
  #define M59_SL_SEL0 0x3B
  #define M60_SL_SEL0 0x3C
  #define M61_SL_SEL0 0x3D
  #define M62_SL_SEL0 0x3E
  #define M63_SL_SEL0 0x3F
  #define M64_SL_SEL0 0x40
  #define M65_SL_SEL0 0x41
  #define M66_SL_SEL0 0x42
  #define M67_SL_SEL0 0x43
  #define M68_SL_SEL0 0x44
  #define M69_SL_SEL0 0x45
  #define M70_SL_SEL0 0x46
  #define M71_SL_SEL0 0x47
  #define M72_SL_SEL0 0x48
  #define M73_SL_SEL0 0x49
  #define M74_SL_SEL0 0x4A
  #define M75_SL_SEL0 0x4B
  #define M76_SL_SEL0 0x4C
  #define M77_SL_SEL0 0x4D
  #define M78_SL_SEL0 0x4E
  #define M79_SL_SEL0 0x4F
  #define M80_SL_SEL0 0x50
  #define M81_SL_SEL0 0x51
  #define M82_SL_SEL0 0x52
  #define M83_SL_SEL0 0x53
  #define M84_SL_SEL0 0x54
  #define M85_SL_SEL0 0x55
  #define M86_SL_SEL0 0x56
  #define M87_SL_SEL0 0x57
  #define M88_SL_SEL0 0x58
  #define M89_SL_SEL0 0x59
  #define M90_SL_SEL0 0x5A
  #define M91_SL_SEL0 0x5B
  #define M92_SL_SEL0 0x5C
  #define M93_SL_SEL0 0x5D
  #define M94_SL_SEL0 0x5E
  #define M95_SL_SEL0 0x5F
  #define M96_SL_SEL0 0x60
  #define M97_SL_SEL0 0x61
  #define M98_SL_SEL0 0x62
  #define M99_SL_SEL0 0x63
  #define M100_SL_SEL0 0x64
  #define M101_SL_SEL0 0x65
  #define M102_SL_SEL0 0x66
  #define M103_SL_SEL0 0x67
  #define M104_SL_SEL0 0x68
  #define M105_SL_SEL0 0x69
  #define M106_SL_SEL0 0x6A
  #define M107_SL_SEL0 0x6B
  #define M108_SL_SEL0 0x6C
  #define M109_SL_SEL0 0x6D
  #define M110_SL_SEL0 0x6E
  #define M111_SL_SEL0 0x6F
  #define M112_SL_SEL0 0x70
  #define M113_SL_SEL0 0x71
  #define M114_SL_SEL0 0x72
  #define M115_SL_SEL0 0x73
  #define M116_SL_SEL0 0x74
  #define M117_SL_SEL0 0x75
  #define M118_SL_SEL0 0x76
  #define M119_SL_SEL0 0x77
  #define M120_SL_SEL0 0x78
  #define M121_SL_SEL0 0x79
  #define M122_SL_SEL0 0x7A
  #define M123_SL_SEL0 0x7B
  #define M124_SL_SEL0 0x7C
  #define M125_SL_SEL0 0x7D
  #define M126_SL_SEL0 0x7E
  #define M127_SL_SEL0 0x7F
#define M_SH_SEL0 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_sl_sel0:7;
        REGWORD v_sh_sel0:1;
    } bit_r_sl_sel0;

    typedef union {REGWORD reg; bit_r_sl_sel0 bit;} reg_r_sl_sel0; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_SL_SEL1 0x15

#define M_SL_SEL1 0x7F
  #define M1_SL_SEL1 0x01
  #define M2_SL_SEL1 0x02
  #define M3_SL_SEL1 0x03
  #define M4_SL_SEL1 0x04
  #define M5_SL_SEL1 0x05
  #define M6_SL_SEL1 0x06
  #define M7_SL_SEL1 0x07
  #define M8_SL_SEL1 0x08
  #define M9_SL_SEL1 0x09
  #define M10_SL_SEL1 0x0A
  #define M11_SL_SEL1 0x0B
  #define M12_SL_SEL1 0x0C
  #define M13_SL_SEL1 0x0D
  #define M14_SL_SEL1 0x0E
  #define M15_SL_SEL1 0x0F
  #define M16_SL_SEL1 0x10
  #define M17_SL_SEL1 0x11
  #define M18_SL_SEL1 0x12
  #define M19_SL_SEL1 0x13
  #define M20_SL_SEL1 0x14
  #define M21_SL_SEL1 0x15
  #define M22_SL_SEL1 0x16
  #define M23_SL_SEL1 0x17
  #define M24_SL_SEL1 0x18
  #define M25_SL_SEL1 0x19
  #define M26_SL_SEL1 0x1A
  #define M27_SL_SEL1 0x1B
  #define M28_SL_SEL1 0x1C
  #define M29_SL_SEL1 0x1D
  #define M30_SL_SEL1 0x1E
  #define M31_SL_SEL1 0x1F
  #define M32_SL_SEL1 0x20
  #define M33_SL_SEL1 0x21
  #define M34_SL_SEL1 0x22
  #define M35_SL_SEL1 0x23
  #define M36_SL_SEL1 0x24
  #define M37_SL_SEL1 0x25
  #define M38_SL_SEL1 0x26
  #define M39_SL_SEL1 0x27
  #define M40_SL_SEL1 0x28
  #define M41_SL_SEL1 0x29
  #define M42_SL_SEL1 0x2A
  #define M43_SL_SEL1 0x2B
  #define M44_SL_SEL1 0x2C
  #define M45_SL_SEL1 0x2D
  #define M46_SL_SEL1 0x2E
  #define M47_SL_SEL1 0x2F
  #define M48_SL_SEL1 0x30
  #define M49_SL_SEL1 0x31
  #define M50_SL_SEL1 0x32
  #define M51_SL_SEL1 0x33
  #define M52_SL_SEL1 0x34
  #define M53_SL_SEL1 0x35
  #define M54_SL_SEL1 0x36
  #define M55_SL_SEL1 0x37
  #define M56_SL_SEL1 0x38
  #define M57_SL_SEL1 0x39
  #define M58_SL_SEL1 0x3A
  #define M59_SL_SEL1 0x3B
  #define M60_SL_SEL1 0x3C
  #define M61_SL_SEL1 0x3D
  #define M62_SL_SEL1 0x3E
  #define M63_SL_SEL1 0x3F
  #define M64_SL_SEL1 0x40
  #define M65_SL_SEL1 0x41
  #define M66_SL_SEL1 0x42
  #define M67_SL_SEL1 0x43
  #define M68_SL_SEL1 0x44
  #define M69_SL_SEL1 0x45
  #define M70_SL_SEL1 0x46
  #define M71_SL_SEL1 0x47
  #define M72_SL_SEL1 0x48
  #define M73_SL_SEL1 0x49
  #define M74_SL_SEL1 0x4A
  #define M75_SL_SEL1 0x4B
  #define M76_SL_SEL1 0x4C
  #define M77_SL_SEL1 0x4D
  #define M78_SL_SEL1 0x4E
  #define M79_SL_SEL1 0x4F
  #define M80_SL_SEL1 0x50
  #define M81_SL_SEL1 0x51
  #define M82_SL_SEL1 0x52
  #define M83_SL_SEL1 0x53
  #define M84_SL_SEL1 0x54
  #define M85_SL_SEL1 0x55
  #define M86_SL_SEL1 0x56
  #define M87_SL_SEL1 0x57
  #define M88_SL_SEL1 0x58
  #define M89_SL_SEL1 0x59
  #define M90_SL_SEL1 0x5A
  #define M91_SL_SEL1 0x5B
  #define M92_SL_SEL1 0x5C
  #define M93_SL_SEL1 0x5D
  #define M94_SL_SEL1 0x5E
  #define M95_SL_SEL1 0x5F
  #define M96_SL_SEL1 0x60
  #define M97_SL_SEL1 0x61
  #define M98_SL_SEL1 0x62
  #define M99_SL_SEL1 0x63
  #define M100_SL_SEL1 0x64
  #define M101_SL_SEL1 0x65
  #define M102_SL_SEL1 0x66
  #define M103_SL_SEL1 0x67
  #define M104_SL_SEL1 0x68
  #define M105_SL_SEL1 0x69
  #define M106_SL_SEL1 0x6A
  #define M107_SL_SEL1 0x6B
  #define M108_SL_SEL1 0x6C
  #define M109_SL_SEL1 0x6D
  #define M110_SL_SEL1 0x6E
  #define M111_SL_SEL1 0x6F
  #define M112_SL_SEL1 0x70
  #define M113_SL_SEL1 0x71
  #define M114_SL_SEL1 0x72
  #define M115_SL_SEL1 0x73
  #define M116_SL_SEL1 0x74
  #define M117_SL_SEL1 0x75
  #define M118_SL_SEL1 0x76
  #define M119_SL_SEL1 0x77
  #define M120_SL_SEL1 0x78
  #define M121_SL_SEL1 0x79
  #define M122_SL_SEL1 0x7A
  #define M123_SL_SEL1 0x7B
  #define M124_SL_SEL1 0x7C
  #define M125_SL_SEL1 0x7D
  #define M126_SL_SEL1 0x7E
  #define M127_SL_SEL1 0x7F
#define M_SH_SEL1 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_sl_sel1:7;
        REGWORD v_sh_sel1:1;
    } bit_r_sl_sel1;

    typedef union {REGWORD reg; bit_r_sl_sel1 bit;} reg_r_sl_sel1; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_SL_SEL2 0x15

#define M_SL_SEL2 0x7F
  #define M1_SL_SEL2 0x01
  #define M2_SL_SEL2 0x02
  #define M3_SL_SEL2 0x03
  #define M4_SL_SEL2 0x04
  #define M5_SL_SEL2 0x05
  #define M6_SL_SEL2 0x06
  #define M7_SL_SEL2 0x07
  #define M8_SL_SEL2 0x08
  #define M9_SL_SEL2 0x09
  #define M10_SL_SEL2 0x0A
  #define M11_SL_SEL2 0x0B
  #define M12_SL_SEL2 0x0C
  #define M13_SL_SEL2 0x0D
  #define M14_SL_SEL2 0x0E
  #define M15_SL_SEL2 0x0F
  #define M16_SL_SEL2 0x10
  #define M17_SL_SEL2 0x11
  #define M18_SL_SEL2 0x12
  #define M19_SL_SEL2 0x13
  #define M20_SL_SEL2 0x14
  #define M21_SL_SEL2 0x15
  #define M22_SL_SEL2 0x16
  #define M23_SL_SEL2 0x17
  #define M24_SL_SEL2 0x18
  #define M25_SL_SEL2 0x19
  #define M26_SL_SEL2 0x1A
  #define M27_SL_SEL2 0x1B
  #define M28_SL_SEL2 0x1C
  #define M29_SL_SEL2 0x1D
  #define M30_SL_SEL2 0x1E
  #define M31_SL_SEL2 0x1F
  #define M32_SL_SEL2 0x20
  #define M33_SL_SEL2 0x21
  #define M34_SL_SEL2 0x22
  #define M35_SL_SEL2 0x23
  #define M36_SL_SEL2 0x24
  #define M37_SL_SEL2 0x25
  #define M38_SL_SEL2 0x26
  #define M39_SL_SEL2 0x27
  #define M40_SL_SEL2 0x28
  #define M41_SL_SEL2 0x29
  #define M42_SL_SEL2 0x2A
  #define M43_SL_SEL2 0x2B
  #define M44_SL_SEL2 0x2C
  #define M45_SL_SEL2 0x2D
  #define M46_SL_SEL2 0x2E
  #define M47_SL_SEL2 0x2F
  #define M48_SL_SEL2 0x30
  #define M49_SL_SEL2 0x31
  #define M50_SL_SEL2 0x32
  #define M51_SL_SEL2 0x33
  #define M52_SL_SEL2 0x34
  #define M53_SL_SEL2 0x35
  #define M54_SL_SEL2 0x36
  #define M55_SL_SEL2 0x37
  #define M56_SL_SEL2 0x38
  #define M57_SL_SEL2 0x39
  #define M58_SL_SEL2 0x3A
  #define M59_SL_SEL2 0x3B
  #define M60_SL_SEL2 0x3C
  #define M61_SL_SEL2 0x3D
  #define M62_SL_SEL2 0x3E
  #define M63_SL_SEL2 0x3F
  #define M64_SL_SEL2 0x40
  #define M65_SL_SEL2 0x41
  #define M66_SL_SEL2 0x42
  #define M67_SL_SEL2 0x43
  #define M68_SL_SEL2 0x44
  #define M69_SL_SEL2 0x45
  #define M70_SL_SEL2 0x46
  #define M71_SL_SEL2 0x47
  #define M72_SL_SEL2 0x48
  #define M73_SL_SEL2 0x49
  #define M74_SL_SEL2 0x4A
  #define M75_SL_SEL2 0x4B
  #define M76_SL_SEL2 0x4C
  #define M77_SL_SEL2 0x4D
  #define M78_SL_SEL2 0x4E
  #define M79_SL_SEL2 0x4F
  #define M80_SL_SEL2 0x50
  #define M81_SL_SEL2 0x51
  #define M82_SL_SEL2 0x52
  #define M83_SL_SEL2 0x53
  #define M84_SL_SEL2 0x54
  #define M85_SL_SEL2 0x55
  #define M86_SL_SEL2 0x56
  #define M87_SL_SEL2 0x57
  #define M88_SL_SEL2 0x58
  #define M89_SL_SEL2 0x59
  #define M90_SL_SEL2 0x5A
  #define M91_SL_SEL2 0x5B
  #define M92_SL_SEL2 0x5C
  #define M93_SL_SEL2 0x5D
  #define M94_SL_SEL2 0x5E
  #define M95_SL_SEL2 0x5F
  #define M96_SL_SEL2 0x60
  #define M97_SL_SEL2 0x61
  #define M98_SL_SEL2 0x62
  #define M99_SL_SEL2 0x63
  #define M100_SL_SEL2 0x64
  #define M101_SL_SEL2 0x65
  #define M102_SL_SEL2 0x66
  #define M103_SL_SEL2 0x67
  #define M104_SL_SEL2 0x68
  #define M105_SL_SEL2 0x69
  #define M106_SL_SEL2 0x6A
  #define M107_SL_SEL2 0x6B
  #define M108_SL_SEL2 0x6C
  #define M109_SL_SEL2 0x6D
  #define M110_SL_SEL2 0x6E
  #define M111_SL_SEL2 0x6F
  #define M112_SL_SEL2 0x70
  #define M113_SL_SEL2 0x71
  #define M114_SL_SEL2 0x72
  #define M115_SL_SEL2 0x73
  #define M116_SL_SEL2 0x74
  #define M117_SL_SEL2 0x75
  #define M118_SL_SEL2 0x76
  #define M119_SL_SEL2 0x77
  #define M120_SL_SEL2 0x78
  #define M121_SL_SEL2 0x79
  #define M122_SL_SEL2 0x7A
  #define M123_SL_SEL2 0x7B
  #define M124_SL_SEL2 0x7C
  #define M125_SL_SEL2 0x7D
  #define M126_SL_SEL2 0x7E
  #define M127_SL_SEL2 0x7F
#define M_SH_SEL2 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_sl_sel2:7;
        REGWORD v_sh_sel2:1;
    } bit_r_sl_sel2;

    typedef union {REGWORD reg; bit_r_sl_sel2 bit;} reg_r_sl_sel2; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_SL_SEL3 0x15

#define M_SL_SEL3 0x7F
  #define M1_SL_SEL3 0x01
  #define M2_SL_SEL3 0x02
  #define M3_SL_SEL3 0x03
  #define M4_SL_SEL3 0x04
  #define M5_SL_SEL3 0x05
  #define M6_SL_SEL3 0x06
  #define M7_SL_SEL3 0x07
  #define M8_SL_SEL3 0x08
  #define M9_SL_SEL3 0x09
  #define M10_SL_SEL3 0x0A
  #define M11_SL_SEL3 0x0B
  #define M12_SL_SEL3 0x0C
  #define M13_SL_SEL3 0x0D
  #define M14_SL_SEL3 0x0E
  #define M15_SL_SEL3 0x0F
  #define M16_SL_SEL3 0x10
  #define M17_SL_SEL3 0x11
  #define M18_SL_SEL3 0x12
  #define M19_SL_SEL3 0x13
  #define M20_SL_SEL3 0x14
  #define M21_SL_SEL3 0x15
  #define M22_SL_SEL3 0x16
  #define M23_SL_SEL3 0x17
  #define M24_SL_SEL3 0x18
  #define M25_SL_SEL3 0x19
  #define M26_SL_SEL3 0x1A
  #define M27_SL_SEL3 0x1B
  #define M28_SL_SEL3 0x1C
  #define M29_SL_SEL3 0x1D
  #define M30_SL_SEL3 0x1E
  #define M31_SL_SEL3 0x1F
  #define M32_SL_SEL3 0x20
  #define M33_SL_SEL3 0x21
  #define M34_SL_SEL3 0x22
  #define M35_SL_SEL3 0x23
  #define M36_SL_SEL3 0x24
  #define M37_SL_SEL3 0x25
  #define M38_SL_SEL3 0x26
  #define M39_SL_SEL3 0x27
  #define M40_SL_SEL3 0x28
  #define M41_SL_SEL3 0x29
  #define M42_SL_SEL3 0x2A
  #define M43_SL_SEL3 0x2B
  #define M44_SL_SEL3 0x2C
  #define M45_SL_SEL3 0x2D
  #define M46_SL_SEL3 0x2E
  #define M47_SL_SEL3 0x2F
  #define M48_SL_SEL3 0x30
  #define M49_SL_SEL3 0x31
  #define M50_SL_SEL3 0x32
  #define M51_SL_SEL3 0x33
  #define M52_SL_SEL3 0x34
  #define M53_SL_SEL3 0x35
  #define M54_SL_SEL3 0x36
  #define M55_SL_SEL3 0x37
  #define M56_SL_SEL3 0x38
  #define M57_SL_SEL3 0x39
  #define M58_SL_SEL3 0x3A
  #define M59_SL_SEL3 0x3B
  #define M60_SL_SEL3 0x3C
  #define M61_SL_SEL3 0x3D
  #define M62_SL_SEL3 0x3E
  #define M63_SL_SEL3 0x3F
  #define M64_SL_SEL3 0x40
  #define M65_SL_SEL3 0x41
  #define M66_SL_SEL3 0x42
  #define M67_SL_SEL3 0x43
  #define M68_SL_SEL3 0x44
  #define M69_SL_SEL3 0x45
  #define M70_SL_SEL3 0x46
  #define M71_SL_SEL3 0x47
  #define M72_SL_SEL3 0x48
  #define M73_SL_SEL3 0x49
  #define M74_SL_SEL3 0x4A
  #define M75_SL_SEL3 0x4B
  #define M76_SL_SEL3 0x4C
  #define M77_SL_SEL3 0x4D
  #define M78_SL_SEL3 0x4E
  #define M79_SL_SEL3 0x4F
  #define M80_SL_SEL3 0x50
  #define M81_SL_SEL3 0x51
  #define M82_SL_SEL3 0x52
  #define M83_SL_SEL3 0x53
  #define M84_SL_SEL3 0x54
  #define M85_SL_SEL3 0x55
  #define M86_SL_SEL3 0x56
  #define M87_SL_SEL3 0x57
  #define M88_SL_SEL3 0x58
  #define M89_SL_SEL3 0x59
  #define M90_SL_SEL3 0x5A
  #define M91_SL_SEL3 0x5B
  #define M92_SL_SEL3 0x5C
  #define M93_SL_SEL3 0x5D
  #define M94_SL_SEL3 0x5E
  #define M95_SL_SEL3 0x5F
  #define M96_SL_SEL3 0x60
  #define M97_SL_SEL3 0x61
  #define M98_SL_SEL3 0x62
  #define M99_SL_SEL3 0x63
  #define M100_SL_SEL3 0x64
  #define M101_SL_SEL3 0x65
  #define M102_SL_SEL3 0x66
  #define M103_SL_SEL3 0x67
  #define M104_SL_SEL3 0x68
  #define M105_SL_SEL3 0x69
  #define M106_SL_SEL3 0x6A
  #define M107_SL_SEL3 0x6B
  #define M108_SL_SEL3 0x6C
  #define M109_SL_SEL3 0x6D
  #define M110_SL_SEL3 0x6E
  #define M111_SL_SEL3 0x6F
  #define M112_SL_SEL3 0x70
  #define M113_SL_SEL3 0x71
  #define M114_SL_SEL3 0x72
  #define M115_SL_SEL3 0x73
  #define M116_SL_SEL3 0x74
  #define M117_SL_SEL3 0x75
  #define M118_SL_SEL3 0x76
  #define M119_SL_SEL3 0x77
  #define M120_SL_SEL3 0x78
  #define M121_SL_SEL3 0x79
  #define M122_SL_SEL3 0x7A
  #define M123_SL_SEL3 0x7B
  #define M124_SL_SEL3 0x7C
  #define M125_SL_SEL3 0x7D
  #define M126_SL_SEL3 0x7E
  #define M127_SL_SEL3 0x7F
#define M_SH_SEL3 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_sl_sel3:7;
        REGWORD v_sh_sel3:1;
    } bit_r_sl_sel3;

    typedef union {REGWORD reg; bit_r_sl_sel3 bit;} reg_r_sl_sel3; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_SL_SEL4 0x15

#define M_SL_SEL4 0x7F
  #define M1_SL_SEL4 0x01
  #define M2_SL_SEL4 0x02
  #define M3_SL_SEL4 0x03
  #define M4_SL_SEL4 0x04
  #define M5_SL_SEL4 0x05
  #define M6_SL_SEL4 0x06
  #define M7_SL_SEL4 0x07
  #define M8_SL_SEL4 0x08
  #define M9_SL_SEL4 0x09
  #define M10_SL_SEL4 0x0A
  #define M11_SL_SEL4 0x0B
  #define M12_SL_SEL4 0x0C
  #define M13_SL_SEL4 0x0D
  #define M14_SL_SEL4 0x0E
  #define M15_SL_SEL4 0x0F
  #define M16_SL_SEL4 0x10
  #define M17_SL_SEL4 0x11
  #define M18_SL_SEL4 0x12
  #define M19_SL_SEL4 0x13
  #define M20_SL_SEL4 0x14
  #define M21_SL_SEL4 0x15
  #define M22_SL_SEL4 0x16
  #define M23_SL_SEL4 0x17
  #define M24_SL_SEL4 0x18
  #define M25_SL_SEL4 0x19
  #define M26_SL_SEL4 0x1A
  #define M27_SL_SEL4 0x1B
  #define M28_SL_SEL4 0x1C
  #define M29_SL_SEL4 0x1D
  #define M30_SL_SEL4 0x1E
  #define M31_SL_SEL4 0x1F
  #define M32_SL_SEL4 0x20
  #define M33_SL_SEL4 0x21
  #define M34_SL_SEL4 0x22
  #define M35_SL_SEL4 0x23
  #define M36_SL_SEL4 0x24
  #define M37_SL_SEL4 0x25
  #define M38_SL_SEL4 0x26
  #define M39_SL_SEL4 0x27
  #define M40_SL_SEL4 0x28
  #define M41_SL_SEL4 0x29
  #define M42_SL_SEL4 0x2A
  #define M43_SL_SEL4 0x2B
  #define M44_SL_SEL4 0x2C
  #define M45_SL_SEL4 0x2D
  #define M46_SL_SEL4 0x2E
  #define M47_SL_SEL4 0x2F
  #define M48_SL_SEL4 0x30
  #define M49_SL_SEL4 0x31
  #define M50_SL_SEL4 0x32
  #define M51_SL_SEL4 0x33
  #define M52_SL_SEL4 0x34
  #define M53_SL_SEL4 0x35
  #define M54_SL_SEL4 0x36
  #define M55_SL_SEL4 0x37
  #define M56_SL_SEL4 0x38
  #define M57_SL_SEL4 0x39
  #define M58_SL_SEL4 0x3A
  #define M59_SL_SEL4 0x3B
  #define M60_SL_SEL4 0x3C
  #define M61_SL_SEL4 0x3D
  #define M62_SL_SEL4 0x3E
  #define M63_SL_SEL4 0x3F
  #define M64_SL_SEL4 0x40
  #define M65_SL_SEL4 0x41
  #define M66_SL_SEL4 0x42
  #define M67_SL_SEL4 0x43
  #define M68_SL_SEL4 0x44
  #define M69_SL_SEL4 0x45
  #define M70_SL_SEL4 0x46
  #define M71_SL_SEL4 0x47
  #define M72_SL_SEL4 0x48
  #define M73_SL_SEL4 0x49
  #define M74_SL_SEL4 0x4A
  #define M75_SL_SEL4 0x4B
  #define M76_SL_SEL4 0x4C
  #define M77_SL_SEL4 0x4D
  #define M78_SL_SEL4 0x4E
  #define M79_SL_SEL4 0x4F
  #define M80_SL_SEL4 0x50
  #define M81_SL_SEL4 0x51
  #define M82_SL_SEL4 0x52
  #define M83_SL_SEL4 0x53
  #define M84_SL_SEL4 0x54
  #define M85_SL_SEL4 0x55
  #define M86_SL_SEL4 0x56
  #define M87_SL_SEL4 0x57
  #define M88_SL_SEL4 0x58
  #define M89_SL_SEL4 0x59
  #define M90_SL_SEL4 0x5A
  #define M91_SL_SEL4 0x5B
  #define M92_SL_SEL4 0x5C
  #define M93_SL_SEL4 0x5D
  #define M94_SL_SEL4 0x5E
  #define M95_SL_SEL4 0x5F
  #define M96_SL_SEL4 0x60
  #define M97_SL_SEL4 0x61
  #define M98_SL_SEL4 0x62
  #define M99_SL_SEL4 0x63
  #define M100_SL_SEL4 0x64
  #define M101_SL_SEL4 0x65
  #define M102_SL_SEL4 0x66
  #define M103_SL_SEL4 0x67
  #define M104_SL_SEL4 0x68
  #define M105_SL_SEL4 0x69
  #define M106_SL_SEL4 0x6A
  #define M107_SL_SEL4 0x6B
  #define M108_SL_SEL4 0x6C
  #define M109_SL_SEL4 0x6D
  #define M110_SL_SEL4 0x6E
  #define M111_SL_SEL4 0x6F
  #define M112_SL_SEL4 0x70
  #define M113_SL_SEL4 0x71
  #define M114_SL_SEL4 0x72
  #define M115_SL_SEL4 0x73
  #define M116_SL_SEL4 0x74
  #define M117_SL_SEL4 0x75
  #define M118_SL_SEL4 0x76
  #define M119_SL_SEL4 0x77
  #define M120_SL_SEL4 0x78
  #define M121_SL_SEL4 0x79
  #define M122_SL_SEL4 0x7A
  #define M123_SL_SEL4 0x7B
  #define M124_SL_SEL4 0x7C
  #define M125_SL_SEL4 0x7D
  #define M126_SL_SEL4 0x7E
  #define M127_SL_SEL4 0x7F
#define M_SH_SEL4 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_sl_sel4:7;
        REGWORD v_sh_sel4:1;
    } bit_r_sl_sel4;

    typedef union {REGWORD reg; bit_r_sl_sel4 bit;} reg_r_sl_sel4; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_SL_SEL5 0x15

#define M_SL_SEL5 0x7F
  #define M1_SL_SEL5 0x01
  #define M2_SL_SEL5 0x02
  #define M3_SL_SEL5 0x03
  #define M4_SL_SEL5 0x04
  #define M5_SL_SEL5 0x05
  #define M6_SL_SEL5 0x06
  #define M7_SL_SEL5 0x07
  #define M8_SL_SEL5 0x08
  #define M9_SL_SEL5 0x09
  #define M10_SL_SEL5 0x0A
  #define M11_SL_SEL5 0x0B
  #define M12_SL_SEL5 0x0C
  #define M13_SL_SEL5 0x0D
  #define M14_SL_SEL5 0x0E
  #define M15_SL_SEL5 0x0F
  #define M16_SL_SEL5 0x10
  #define M17_SL_SEL5 0x11
  #define M18_SL_SEL5 0x12
  #define M19_SL_SEL5 0x13
  #define M20_SL_SEL5 0x14
  #define M21_SL_SEL5 0x15
  #define M22_SL_SEL5 0x16
  #define M23_SL_SEL5 0x17
  #define M24_SL_SEL5 0x18
  #define M25_SL_SEL5 0x19
  #define M26_SL_SEL5 0x1A
  #define M27_SL_SEL5 0x1B
  #define M28_SL_SEL5 0x1C
  #define M29_SL_SEL5 0x1D
  #define M30_SL_SEL5 0x1E
  #define M31_SL_SEL5 0x1F
  #define M32_SL_SEL5 0x20
  #define M33_SL_SEL5 0x21
  #define M34_SL_SEL5 0x22
  #define M35_SL_SEL5 0x23
  #define M36_SL_SEL5 0x24
  #define M37_SL_SEL5 0x25
  #define M38_SL_SEL5 0x26
  #define M39_SL_SEL5 0x27
  #define M40_SL_SEL5 0x28
  #define M41_SL_SEL5 0x29
  #define M42_SL_SEL5 0x2A
  #define M43_SL_SEL5 0x2B
  #define M44_SL_SEL5 0x2C
  #define M45_SL_SEL5 0x2D
  #define M46_SL_SEL5 0x2E
  #define M47_SL_SEL5 0x2F
  #define M48_SL_SEL5 0x30
  #define M49_SL_SEL5 0x31
  #define M50_SL_SEL5 0x32
  #define M51_SL_SEL5 0x33
  #define M52_SL_SEL5 0x34
  #define M53_SL_SEL5 0x35
  #define M54_SL_SEL5 0x36
  #define M55_SL_SEL5 0x37
  #define M56_SL_SEL5 0x38
  #define M57_SL_SEL5 0x39
  #define M58_SL_SEL5 0x3A
  #define M59_SL_SEL5 0x3B
  #define M60_SL_SEL5 0x3C
  #define M61_SL_SEL5 0x3D
  #define M62_SL_SEL5 0x3E
  #define M63_SL_SEL5 0x3F
  #define M64_SL_SEL5 0x40
  #define M65_SL_SEL5 0x41
  #define M66_SL_SEL5 0x42
  #define M67_SL_SEL5 0x43
  #define M68_SL_SEL5 0x44
  #define M69_SL_SEL5 0x45
  #define M70_SL_SEL5 0x46
  #define M71_SL_SEL5 0x47
  #define M72_SL_SEL5 0x48
  #define M73_SL_SEL5 0x49
  #define M74_SL_SEL5 0x4A
  #define M75_SL_SEL5 0x4B
  #define M76_SL_SEL5 0x4C
  #define M77_SL_SEL5 0x4D
  #define M78_SL_SEL5 0x4E
  #define M79_SL_SEL5 0x4F
  #define M80_SL_SEL5 0x50
  #define M81_SL_SEL5 0x51
  #define M82_SL_SEL5 0x52
  #define M83_SL_SEL5 0x53
  #define M84_SL_SEL5 0x54
  #define M85_SL_SEL5 0x55
  #define M86_SL_SEL5 0x56
  #define M87_SL_SEL5 0x57
  #define M88_SL_SEL5 0x58
  #define M89_SL_SEL5 0x59
  #define M90_SL_SEL5 0x5A
  #define M91_SL_SEL5 0x5B
  #define M92_SL_SEL5 0x5C
  #define M93_SL_SEL5 0x5D
  #define M94_SL_SEL5 0x5E
  #define M95_SL_SEL5 0x5F
  #define M96_SL_SEL5 0x60
  #define M97_SL_SEL5 0x61
  #define M98_SL_SEL5 0x62
  #define M99_SL_SEL5 0x63
  #define M100_SL_SEL5 0x64
  #define M101_SL_SEL5 0x65
  #define M102_SL_SEL5 0x66
  #define M103_SL_SEL5 0x67
  #define M104_SL_SEL5 0x68
  #define M105_SL_SEL5 0x69
  #define M106_SL_SEL5 0x6A
  #define M107_SL_SEL5 0x6B
  #define M108_SL_SEL5 0x6C
  #define M109_SL_SEL5 0x6D
  #define M110_SL_SEL5 0x6E
  #define M111_SL_SEL5 0x6F
  #define M112_SL_SEL5 0x70
  #define M113_SL_SEL5 0x71
  #define M114_SL_SEL5 0x72
  #define M115_SL_SEL5 0x73
  #define M116_SL_SEL5 0x74
  #define M117_SL_SEL5 0x75
  #define M118_SL_SEL5 0x76
  #define M119_SL_SEL5 0x77
  #define M120_SL_SEL5 0x78
  #define M121_SL_SEL5 0x79
  #define M122_SL_SEL5 0x7A
  #define M123_SL_SEL5 0x7B
  #define M124_SL_SEL5 0x7C
  #define M125_SL_SEL5 0x7D
  #define M126_SL_SEL5 0x7E
  #define M127_SL_SEL5 0x7F
#define M_SH_SEL5 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_sl_sel5:7;
        REGWORD v_sh_sel5:1;
    } bit_r_sl_sel5;

    typedef union {REGWORD reg; bit_r_sl_sel5 bit;} reg_r_sl_sel5; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_SL_SEL6 0x15

#define M_SL_SEL6 0x7F
  #define M1_SL_SEL6 0x01
  #define M2_SL_SEL6 0x02
  #define M3_SL_SEL6 0x03
  #define M4_SL_SEL6 0x04
  #define M5_SL_SEL6 0x05
  #define M6_SL_SEL6 0x06
  #define M7_SL_SEL6 0x07
  #define M8_SL_SEL6 0x08
  #define M9_SL_SEL6 0x09
  #define M10_SL_SEL6 0x0A
  #define M11_SL_SEL6 0x0B
  #define M12_SL_SEL6 0x0C
  #define M13_SL_SEL6 0x0D
  #define M14_SL_SEL6 0x0E
  #define M15_SL_SEL6 0x0F
  #define M16_SL_SEL6 0x10
  #define M17_SL_SEL6 0x11
  #define M18_SL_SEL6 0x12
  #define M19_SL_SEL6 0x13
  #define M20_SL_SEL6 0x14
  #define M21_SL_SEL6 0x15
  #define M22_SL_SEL6 0x16
  #define M23_SL_SEL6 0x17
  #define M24_SL_SEL6 0x18
  #define M25_SL_SEL6 0x19
  #define M26_SL_SEL6 0x1A
  #define M27_SL_SEL6 0x1B
  #define M28_SL_SEL6 0x1C
  #define M29_SL_SEL6 0x1D
  #define M30_SL_SEL6 0x1E
  #define M31_SL_SEL6 0x1F
  #define M32_SL_SEL6 0x20
  #define M33_SL_SEL6 0x21
  #define M34_SL_SEL6 0x22
  #define M35_SL_SEL6 0x23
  #define M36_SL_SEL6 0x24
  #define M37_SL_SEL6 0x25
  #define M38_SL_SEL6 0x26
  #define M39_SL_SEL6 0x27
  #define M40_SL_SEL6 0x28
  #define M41_SL_SEL6 0x29
  #define M42_SL_SEL6 0x2A
  #define M43_SL_SEL6 0x2B
  #define M44_SL_SEL6 0x2C
  #define M45_SL_SEL6 0x2D
  #define M46_SL_SEL6 0x2E
  #define M47_SL_SEL6 0x2F
  #define M48_SL_SEL6 0x30
  #define M49_SL_SEL6 0x31
  #define M50_SL_SEL6 0x32
  #define M51_SL_SEL6 0x33
  #define M52_SL_SEL6 0x34
  #define M53_SL_SEL6 0x35
  #define M54_SL_SEL6 0x36
  #define M55_SL_SEL6 0x37
  #define M56_SL_SEL6 0x38
  #define M57_SL_SEL6 0x39
  #define M58_SL_SEL6 0x3A
  #define M59_SL_SEL6 0x3B
  #define M60_SL_SEL6 0x3C
  #define M61_SL_SEL6 0x3D
  #define M62_SL_SEL6 0x3E
  #define M63_SL_SEL6 0x3F
  #define M64_SL_SEL6 0x40
  #define M65_SL_SEL6 0x41
  #define M66_SL_SEL6 0x42
  #define M67_SL_SEL6 0x43
  #define M68_SL_SEL6 0x44
  #define M69_SL_SEL6 0x45
  #define M70_SL_SEL6 0x46
  #define M71_SL_SEL6 0x47
  #define M72_SL_SEL6 0x48
  #define M73_SL_SEL6 0x49
  #define M74_SL_SEL6 0x4A
  #define M75_SL_SEL6 0x4B
  #define M76_SL_SEL6 0x4C
  #define M77_SL_SEL6 0x4D
  #define M78_SL_SEL6 0x4E
  #define M79_SL_SEL6 0x4F
  #define M80_SL_SEL6 0x50
  #define M81_SL_SEL6 0x51
  #define M82_SL_SEL6 0x52
  #define M83_SL_SEL6 0x53
  #define M84_SL_SEL6 0x54
  #define M85_SL_SEL6 0x55
  #define M86_SL_SEL6 0x56
  #define M87_SL_SEL6 0x57
  #define M88_SL_SEL6 0x58
  #define M89_SL_SEL6 0x59
  #define M90_SL_SEL6 0x5A
  #define M91_SL_SEL6 0x5B
  #define M92_SL_SEL6 0x5C
  #define M93_SL_SEL6 0x5D
  #define M94_SL_SEL6 0x5E
  #define M95_SL_SEL6 0x5F
  #define M96_SL_SEL6 0x60
  #define M97_SL_SEL6 0x61
  #define M98_SL_SEL6 0x62
  #define M99_SL_SEL6 0x63
  #define M100_SL_SEL6 0x64
  #define M101_SL_SEL6 0x65
  #define M102_SL_SEL6 0x66
  #define M103_SL_SEL6 0x67
  #define M104_SL_SEL6 0x68
  #define M105_SL_SEL6 0x69
  #define M106_SL_SEL6 0x6A
  #define M107_SL_SEL6 0x6B
  #define M108_SL_SEL6 0x6C
  #define M109_SL_SEL6 0x6D
  #define M110_SL_SEL6 0x6E
  #define M111_SL_SEL6 0x6F
  #define M112_SL_SEL6 0x70
  #define M113_SL_SEL6 0x71
  #define M114_SL_SEL6 0x72
  #define M115_SL_SEL6 0x73
  #define M116_SL_SEL6 0x74
  #define M117_SL_SEL6 0x75
  #define M118_SL_SEL6 0x76
  #define M119_SL_SEL6 0x77
  #define M120_SL_SEL6 0x78
  #define M121_SL_SEL6 0x79
  #define M122_SL_SEL6 0x7A
  #define M123_SL_SEL6 0x7B
  #define M124_SL_SEL6 0x7C
  #define M125_SL_SEL6 0x7D
  #define M126_SL_SEL6 0x7E
  #define M127_SL_SEL6 0x7F
#define M_SH_SEL6 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_sl_sel6:7;
        REGWORD v_sh_sel6:1;
    } bit_r_sl_sel6;

    typedef union {REGWORD reg; bit_r_sl_sel6 bit;} reg_r_sl_sel6; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_SL_SEL7 0x15

#define M_SL_SEL7 0x7F
  #define M1_SL_SEL7 0x01
  #define M2_SL_SEL7 0x02
  #define M3_SL_SEL7 0x03
  #define M4_SL_SEL7 0x04
  #define M5_SL_SEL7 0x05
  #define M6_SL_SEL7 0x06
  #define M7_SL_SEL7 0x07
  #define M8_SL_SEL7 0x08
  #define M9_SL_SEL7 0x09
  #define M10_SL_SEL7 0x0A
  #define M11_SL_SEL7 0x0B
  #define M12_SL_SEL7 0x0C
  #define M13_SL_SEL7 0x0D
  #define M14_SL_SEL7 0x0E
  #define M15_SL_SEL7 0x0F
  #define M16_SL_SEL7 0x10
  #define M17_SL_SEL7 0x11
  #define M18_SL_SEL7 0x12
  #define M19_SL_SEL7 0x13
  #define M20_SL_SEL7 0x14
  #define M21_SL_SEL7 0x15
  #define M22_SL_SEL7 0x16
  #define M23_SL_SEL7 0x17
  #define M24_SL_SEL7 0x18
  #define M25_SL_SEL7 0x19
  #define M26_SL_SEL7 0x1A
  #define M27_SL_SEL7 0x1B
  #define M28_SL_SEL7 0x1C
  #define M29_SL_SEL7 0x1D
  #define M30_SL_SEL7 0x1E
  #define M31_SL_SEL7 0x1F
  #define M32_SL_SEL7 0x20
  #define M33_SL_SEL7 0x21
  #define M34_SL_SEL7 0x22
  #define M35_SL_SEL7 0x23
  #define M36_SL_SEL7 0x24
  #define M37_SL_SEL7 0x25
  #define M38_SL_SEL7 0x26
  #define M39_SL_SEL7 0x27
  #define M40_SL_SEL7 0x28
  #define M41_SL_SEL7 0x29
  #define M42_SL_SEL7 0x2A
  #define M43_SL_SEL7 0x2B
  #define M44_SL_SEL7 0x2C
  #define M45_SL_SEL7 0x2D
  #define M46_SL_SEL7 0x2E
  #define M47_SL_SEL7 0x2F
  #define M48_SL_SEL7 0x30
  #define M49_SL_SEL7 0x31
  #define M50_SL_SEL7 0x32
  #define M51_SL_SEL7 0x33
  #define M52_SL_SEL7 0x34
  #define M53_SL_SEL7 0x35
  #define M54_SL_SEL7 0x36
  #define M55_SL_SEL7 0x37
  #define M56_SL_SEL7 0x38
  #define M57_SL_SEL7 0x39
  #define M58_SL_SEL7 0x3A
  #define M59_SL_SEL7 0x3B
  #define M60_SL_SEL7 0x3C
  #define M61_SL_SEL7 0x3D
  #define M62_SL_SEL7 0x3E
  #define M63_SL_SEL7 0x3F
  #define M64_SL_SEL7 0x40
  #define M65_SL_SEL7 0x41
  #define M66_SL_SEL7 0x42
  #define M67_SL_SEL7 0x43
  #define M68_SL_SEL7 0x44
  #define M69_SL_SEL7 0x45
  #define M70_SL_SEL7 0x46
  #define M71_SL_SEL7 0x47
  #define M72_SL_SEL7 0x48
  #define M73_SL_SEL7 0x49
  #define M74_SL_SEL7 0x4A
  #define M75_SL_SEL7 0x4B
  #define M76_SL_SEL7 0x4C
  #define M77_SL_SEL7 0x4D
  #define M78_SL_SEL7 0x4E
  #define M79_SL_SEL7 0x4F
  #define M80_SL_SEL7 0x50
  #define M81_SL_SEL7 0x51
  #define M82_SL_SEL7 0x52
  #define M83_SL_SEL7 0x53
  #define M84_SL_SEL7 0x54
  #define M85_SL_SEL7 0x55
  #define M86_SL_SEL7 0x56
  #define M87_SL_SEL7 0x57
  #define M88_SL_SEL7 0x58
  #define M89_SL_SEL7 0x59
  #define M90_SL_SEL7 0x5A
  #define M91_SL_SEL7 0x5B
  #define M92_SL_SEL7 0x5C
  #define M93_SL_SEL7 0x5D
  #define M94_SL_SEL7 0x5E
  #define M95_SL_SEL7 0x5F
  #define M96_SL_SEL7 0x60
  #define M97_SL_SEL7 0x61
  #define M98_SL_SEL7 0x62
  #define M99_SL_SEL7 0x63
  #define M100_SL_SEL7 0x64
  #define M101_SL_SEL7 0x65
  #define M102_SL_SEL7 0x66
  #define M103_SL_SEL7 0x67
  #define M104_SL_SEL7 0x68
  #define M105_SL_SEL7 0x69
  #define M106_SL_SEL7 0x6A
  #define M107_SL_SEL7 0x6B
  #define M108_SL_SEL7 0x6C
  #define M109_SL_SEL7 0x6D
  #define M110_SL_SEL7 0x6E
  #define M111_SL_SEL7 0x6F
  #define M112_SL_SEL7 0x70
  #define M113_SL_SEL7 0x71
  #define M114_SL_SEL7 0x72
  #define M115_SL_SEL7 0x73
  #define M116_SL_SEL7 0x74
  #define M117_SL_SEL7 0x75
  #define M118_SL_SEL7 0x76
  #define M119_SL_SEL7 0x77
  #define M120_SL_SEL7 0x78
  #define M121_SL_SEL7 0x79
  #define M122_SL_SEL7 0x7A
  #define M123_SL_SEL7 0x7B
  #define M124_SL_SEL7 0x7C
  #define M125_SL_SEL7 0x7D
  #define M126_SL_SEL7 0x7E
  #define M127_SL_SEL7 0x7F
#define M_SH_SEL7 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_sl_sel7:7;
        REGWORD v_sh_sel7:1;
    } bit_r_sl_sel7;

    typedef union {REGWORD reg; bit_r_sl_sel7 bit;} reg_r_sl_sel7; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_PCM_MD1 0x15

#define M_DEC_CNT 0x01
#define M_PLL_ADJ_SPEED 0x0C
  #define M1_PLL_ADJ_SPEED 0x04
  #define M2_PLL_ADJ_SPEED 0x08
  #define M3_PLL_ADJ_SPEED 0x0C
#define M_PCM_DR 0x30
  #define M1_PCM_DR 0x10
  #define M2_PCM_DR 0x20
  #define M3_PCM_DR 0x30
#define M_PCM_LOOP 0x40

    typedef struct // bitmap construction
    {
        REGWORD v_dec_cnt:1;
        REGWORD reserved_25:1;
        REGWORD v_pll_adj_speed:2;
        REGWORD v_pcm_dr:2;
        REGWORD v_pcm_loop:1;
        REGWORD reserved_26:1;
    } bit_r_pcm_md1;

    typedef union {REGWORD reg; bit_r_pcm_md1 bit;} reg_r_pcm_md1; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_PCM_MD2 0x15

#define M_SYNC_SRC 0x04
#define M_SYNC_OUT 0x08
#define M_ICR_FR_TIME 0x40
#define M_EN_PLL 0x80

    typedef struct // bitmap construction
    {
        REGWORD reserved_27:2;
        REGWORD v_sync_src:1;
        REGWORD v_sync_out:1;
        REGWORD reserved_28:2;
        REGWORD v_icr_fr_time:1;
        REGWORD v_en_pll:1;
    } bit_r_pcm_md2;

    typedef union {REGWORD reg; bit_r_pcm_md2 bit;} reg_r_pcm_md2; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_SH0L 0x15

#define M_SH0L 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_sh0l:8;
    } bit_r_sh0l;

    typedef union {REGWORD reg; bit_r_sh0l bit;} reg_r_sh0l; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_SH0H 0x15

#define M_SH0H 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_sh0h:8;
    } bit_r_sh0h;

    typedef union {REGWORD reg; bit_r_sh0h bit;} reg_r_sh0h; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_SH1L 0x15

#define M_SH1L 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_sh1l:8;
    } bit_r_sh1l;

    typedef union {REGWORD reg; bit_r_sh1l bit;} reg_r_sh1l; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_SH1H 0x15

#define M_SH1H 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_sh1h:8;
    } bit_r_sh1h;

    typedef union {REGWORD reg; bit_r_sh1h bit;} reg_r_sh1h; // register and bitmap access

#define R_IRQ_OVIEW 0x10

#define M_IRQ_FIFO_BL0 0x01
#define M_IRQ_FIFO_BL1 0x02
#define M_IRQ_FIFO_BL2 0x04
#define M_IRQ_FIFO_BL3 0x08
#define M_IRQ_FIFO_BL4 0x10
#define M_IRQ_FIFO_BL5 0x20
#define M_IRQ_FIFO_BL6 0x40
#define M_IRQ_FIFO_BL7 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_irq_fifo_bl0:1;
        REGWORD v_irq_fifo_bl1:1;
        REGWORD v_irq_fifo_bl2:1;
        REGWORD v_irq_fifo_bl3:1;
        REGWORD v_irq_fifo_bl4:1;
        REGWORD v_irq_fifo_bl5:1;
        REGWORD v_irq_fifo_bl6:1;
        REGWORD v_irq_fifo_bl7:1;
    } bit_r_irq_oview;

    typedef union {REGWORD reg; bit_r_irq_oview bit;} reg_r_irq_oview; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_IRQ_MISC 0x11

#define M_TI_IRQ 0x02
#define M_IRQ_PROC 0x04
#define M_DTMF_IRQ 0x08

    typedef struct // bitmap construction
    {
        REGWORD reserved_30:1;
        REGWORD v_ti_irq:1;
        REGWORD v_irq_proc:1;
        REGWORD v_dtmf_irq:1;
        REGWORD reserved_32:1;
        REGWORD reserved_34:1;
        REGWORD reserved_36:1;
        REGWORD reserved_38:1;
    } bit_r_irq_misc;

    typedef union {REGWORD reg; bit_r_irq_misc bit;} reg_r_irq_misc; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_CONF_OFLOW 0x14

#define M_CONF_OFLOW0 0x01
#define M_CONF_OFLOW1 0x02
#define M_CONF_OFLOW2 0x04
#define M_CONF_OFLOW3 0x08
#define M_CONF_OFLOW4 0x10
#define M_CONF_OFLOW5 0x20
#define M_CONF_OFLOW6 0x40
#define M_CONF_OFLOW7 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_conf_oflow0:1;
        REGWORD v_conf_oflow1:1;
        REGWORD v_conf_oflow2:1;
        REGWORD v_conf_oflow3:1;
        REGWORD v_conf_oflow4:1;
        REGWORD v_conf_oflow5:1;
        REGWORD v_conf_oflow6:1;
        REGWORD v_conf_oflow7:1;
    } bit_r_conf_oflow;

    typedef union {REGWORD reg; bit_r_conf_oflow bit;} reg_r_conf_oflow; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_CHIP_ID 0x16
#define R_CHIP_RV 0x1F

#define M_PNP_IRQ 0x0F
  #define M1_PNP_IRQ 0x01
  #define M2_PNP_IRQ 0x02
  #define M3_PNP_IRQ 0x03
  #define M4_PNP_IRQ 0x04
  #define M5_PNP_IRQ 0x05
  #define M6_PNP_IRQ 0x06
  #define M7_PNP_IRQ 0x07
  #define M8_PNP_IRQ 0x08
  #define M9_PNP_IRQ 0x09
  #define M10_PNP_IRQ 0x0A
  #define M11_PNP_IRQ 0x0B
  #define M12_PNP_IRQ 0x0C
  #define M13_PNP_IRQ 0x0D
  #define M14_PNP_IRQ 0x0E
  #define M15_PNP_IRQ 0x0F
#define M_CHIP_ID 0xF0
  #define M1_CHIP_ID 0x10
  #define M2_CHIP_ID 0x20
  #define M3_CHIP_ID 0x30
  #define M4_CHIP_ID 0x40
  #define M5_CHIP_ID 0x50
  #define M6_CHIP_ID 0x60
  #define M7_CHIP_ID 0x70
  #define M8_CHIP_ID 0x80
  #define M9_CHIP_ID 0x90
  #define M10_CHIP_ID 0xA0
  #define M11_CHIP_ID 0xB0
  #define M12_CHIP_ID 0xC0
  #define M13_CHIP_ID 0xD0
  #define M14_CHIP_ID 0xE0
  #define M15_CHIP_ID 0xF0

    typedef struct // bitmap construction
    {
        REGWORD v_pnp_irq:4;
        REGWORD v_chip_id:4;
    } bit_r_chip_id;

    typedef union {REGWORD reg; bit_r_chip_id bit;} reg_r_chip_id; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BERT_STA 0x17

#define M_BERT_SYNC 0x10
#define M_BERT_INV_DATA 0x20

    typedef struct // bitmap construction
    {
        REGWORD reserved_39v_bert_sync_src:4;
        REGWORD v_bert_sync:1;
        REGWORD v_bert_inv_data:1;
        REGWORD reserved_40:2;
    } bit_r_bert_sta;

    typedef union {REGWORD reg; bit_r_bert_sta bit;} reg_r_bert_sta; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_F0_CNTL 0x18

#define M_F0_CNTL 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_f0_cntl:8;
    } bit_r_f0_cntl;

    typedef union {REGWORD reg; bit_r_f0_cntl bit;} reg_r_f0_cntl; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_F0_CNTH 0x19

#define M_F0_CNTH 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_f0_cnth:8;
    } bit_r_f0_cnth;

    typedef union {REGWORD reg; bit_r_f0_cnth bit;} reg_r_f0_cnth; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BERT_ECL 0x1A

#define M_BERT_ECL 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_bert_ecl:8;
    } bit_r_bert_ecl;

    typedef union {REGWORD reg; bit_r_bert_ecl bit;} reg_r_bert_ecl; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BERT_ECH 0x1B

#define M_BERT_ECH 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_bert_ech:8;
    } bit_r_bert_ech;

    typedef union {REGWORD reg; bit_r_bert_ech bit;} reg_r_bert_ech; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_STATUS 0x1C

#define M_BUSY 0x01
#define M_PROC 0x02
#define M_DTMF_IRQSTA 0x04
#define M_LOST_STA 0x08
#define M_SYNC_IN 0x10
#define M_EXT_IRQSTA 0x20
#define M_MISC_IRQSTA 0x40
#define M_FR_IRQSTA 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_busy:1;
        REGWORD v_proc:1;
        REGWORD v_dtmf_irqsta:1;
        REGWORD v_lost_sta:1;
        REGWORD v_sync_in:1;
        REGWORD v_ext_irqsta:1;
        REGWORD v_misc_irqsta:1;
        REGWORD v_fr_irqsta:1;
    } bit_r_status;

    typedef union {REGWORD reg; bit_r_status bit;} reg_r_status; // register and bitmap access

#define R_SLOT 0x10

#define M_SL_DIR 0x01
#define M_SL_NUM 0xFE
  #define M1_SL_NUM 0x02
  #define M2_SL_NUM 0x04
  #define M3_SL_NUM 0x06
  #define M4_SL_NUM 0x08
  #define M5_SL_NUM 0x0A
  #define M6_SL_NUM 0x0C
  #define M7_SL_NUM 0x0E
  #define M8_SL_NUM 0x10
  #define M9_SL_NUM 0x12
  #define M10_SL_NUM 0x14
  #define M11_SL_NUM 0x16
  #define M12_SL_NUM 0x18
  #define M13_SL_NUM 0x1A
  #define M14_SL_NUM 0x1C
  #define M15_SL_NUM 0x1E
  #define M16_SL_NUM 0x20
  #define M17_SL_NUM 0x22
  #define M18_SL_NUM 0x24
  #define M19_SL_NUM 0x26
  #define M20_SL_NUM 0x28
  #define M21_SL_NUM 0x2A
  #define M22_SL_NUM 0x2C
  #define M23_SL_NUM 0x2E
  #define M24_SL_NUM 0x30
  #define M25_SL_NUM 0x32
  #define M26_SL_NUM 0x34
  #define M27_SL_NUM 0x36
  #define M28_SL_NUM 0x38
  #define M29_SL_NUM 0x3A
  #define M30_SL_NUM 0x3C
  #define M31_SL_NUM 0x3E
  #define M32_SL_NUM 0x40
  #define M33_SL_NUM 0x42
  #define M34_SL_NUM 0x44
  #define M35_SL_NUM 0x46
  #define M36_SL_NUM 0x48
  #define M37_SL_NUM 0x4A
  #define M38_SL_NUM 0x4C
  #define M39_SL_NUM 0x4E
  #define M40_SL_NUM 0x50
  #define M41_SL_NUM 0x52
  #define M42_SL_NUM 0x54
  #define M43_SL_NUM 0x56
  #define M44_SL_NUM 0x58
  #define M45_SL_NUM 0x5A
  #define M46_SL_NUM 0x5C
  #define M47_SL_NUM 0x5E
  #define M48_SL_NUM 0x60
  #define M49_SL_NUM 0x62
  #define M50_SL_NUM 0x64
  #define M51_SL_NUM 0x66
  #define M52_SL_NUM 0x68
  #define M53_SL_NUM 0x6A
  #define M54_SL_NUM 0x6C
  #define M55_SL_NUM 0x6E
  #define M56_SL_NUM 0x70
  #define M57_SL_NUM 0x72
  #define M58_SL_NUM 0x74
  #define M59_SL_NUM 0x76
  #define M60_SL_NUM 0x78
  #define M61_SL_NUM 0x7A
  #define M62_SL_NUM 0x7C
  #define M63_SL_NUM 0x7E
  #define M64_SL_NUM 0x80
  #define M65_SL_NUM 0x82
  #define M66_SL_NUM 0x84
  #define M67_SL_NUM 0x86
  #define M68_SL_NUM 0x88
  #define M69_SL_NUM 0x8A
  #define M70_SL_NUM 0x8C
  #define M71_SL_NUM 0x8E
  #define M72_SL_NUM 0x90
  #define M73_SL_NUM 0x92
  #define M74_SL_NUM 0x94
  #define M75_SL_NUM 0x96
  #define M76_SL_NUM 0x98
  #define M77_SL_NUM 0x9A
  #define M78_SL_NUM 0x9C
  #define M79_SL_NUM 0x9E
  #define M80_SL_NUM 0xA0
  #define M81_SL_NUM 0xA2
  #define M82_SL_NUM 0xA4
  #define M83_SL_NUM 0xA6
  #define M84_SL_NUM 0xA8
  #define M85_SL_NUM 0xAA
  #define M86_SL_NUM 0xAC
  #define M87_SL_NUM 0xAE
  #define M88_SL_NUM 0xB0
  #define M89_SL_NUM 0xB2
  #define M90_SL_NUM 0xB4
  #define M91_SL_NUM 0xB6
  #define M92_SL_NUM 0xB8
  #define M93_SL_NUM 0xBA
  #define M94_SL_NUM 0xBC
  #define M95_SL_NUM 0xBE
  #define M96_SL_NUM 0xC0
  #define M97_SL_NUM 0xC2
  #define M98_SL_NUM 0xC4
  #define M99_SL_NUM 0xC6
  #define M100_SL_NUM 0xC8
  #define M101_SL_NUM 0xCA
  #define M102_SL_NUM 0xCC
  #define M103_SL_NUM 0xCE
  #define M104_SL_NUM 0xD0
  #define M105_SL_NUM 0xD2
  #define M106_SL_NUM 0xD4
  #define M107_SL_NUM 0xD6
  #define M108_SL_NUM 0xD8
  #define M109_SL_NUM 0xDA
  #define M110_SL_NUM 0xDC
  #define M111_SL_NUM 0xDE
  #define M112_SL_NUM 0xE0
  #define M113_SL_NUM 0xE2
  #define M114_SL_NUM 0xE4
  #define M115_SL_NUM 0xE6
  #define M116_SL_NUM 0xE8
  #define M117_SL_NUM 0xEA
  #define M118_SL_NUM 0xEC
  #define M119_SL_NUM 0xEE
  #define M120_SL_NUM 0xF0
  #define M121_SL_NUM 0xF2
  #define M122_SL_NUM 0xF4
  #define M123_SL_NUM 0xF6
  #define M124_SL_NUM 0xF8
  #define M125_SL_NUM 0xFA
  #define M126_SL_NUM 0xFC
  #define M127_SL_NUM 0xFE

    typedef struct // bitmap construction
    {
        REGWORD v_sl_dir:1;
        REGWORD v_sl_num:7;
    } bit_r_slot;

    typedef union {REGWORD reg; bit_r_slot bit;} reg_r_slot; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_IRQMSK_MISC 0x11

#define M_TI_IRQMSK 0x02
#define M_PROC_IRQMSK 0x04
#define M_DTMF_IRQMSK 0x08

    typedef struct // bitmap construction
    {
        REGWORD reserved_42:1;
        REGWORD v_ti_irqmsk:1;
        REGWORD v_proc_irqmsk:1;
        REGWORD v_dtmf_irqmsk:1;
        REGWORD reserved_44:1;
        REGWORD reserved_46:1;
        REGWORD reserved_48:1;
        REGWORD reserved_50:1;
    } bit_r_irqmsk_misc;

    typedef union {REGWORD reg; bit_r_irqmsk_misc bit;} reg_r_irqmsk_misc; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_IRQ_CTRL 0x13

#define M_FIFO_IRQ 0x01
#define M_GLOB_IRQ_EN 0x08
#define M_IRQ_POL 0x10

    typedef struct // bitmap construction
    {
        REGWORD v_fifo_irq:1;
        REGWORD reserved_51:2;
        REGWORD v_glob_irq_en:1;
        REGWORD v_irq_pol:1;
        REGWORD reserved_52:3;
    } bit_r_irq_ctrl;

    typedef union {REGWORD reg; bit_r_irq_ctrl bit;} reg_r_irq_ctrl; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_PCM_MD0 0x14

#define M_PCM_MD 0x01
#define M_C4_POL 0x02
#define M_F0_NEG 0x04
#define M_F0_LEN 0x08
#define M_PCM_ADDR 0xF0
  #define M1_PCM_ADDR 0x10
  #define M2_PCM_ADDR 0x20
  #define M3_PCM_ADDR 0x30
  #define M4_PCM_ADDR 0x40
  #define M5_PCM_ADDR 0x50
  #define M6_PCM_ADDR 0x60
  #define M7_PCM_ADDR 0x70
  #define M8_PCM_ADDR 0x80
  #define M9_PCM_ADDR 0x90
  #define M10_PCM_ADDR 0xA0
  #define M11_PCM_ADDR 0xB0
  #define M12_PCM_ADDR 0xC0
  #define M13_PCM_ADDR 0xD0
  #define M14_PCM_ADDR 0xE0
  #define M15_PCM_ADDR 0xF0

    typedef struct // bitmap construction
    {
        REGWORD v_pcm_md:1;
        REGWORD v_c4_pol:1;
        REGWORD v_f0_neg:1;
        REGWORD v_f0_len:1;
        REGWORD v_pcm_addr:4;
    } bit_r_pcm_md0;

    typedef union {REGWORD reg; bit_r_pcm_md0 bit;} reg_r_pcm_md0; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_CONF_EN 0x18

#define M_CONF_EN 0x01
#define M_ULAW 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_conf_en:1;
        REGWORD reserved_53:6;
        REGWORD v_ulaw:1;
    } bit_r_conf_en;

    typedef union {REGWORD reg; bit_r_conf_en bit;} reg_r_conf_en; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_TI_WD 0x1A

#define M_EV_TS 0x0F
  #define M1_EV_TS 0x01
  #define M2_EV_TS 0x02
  #define M3_EV_TS 0x03
  #define M4_EV_TS 0x04
  #define M5_EV_TS 0x05
  #define M6_EV_TS 0x06
  #define M7_EV_TS 0x07
  #define M8_EV_TS 0x08
  #define M9_EV_TS 0x09
  #define M10_EV_TS 0x0A
  #define M11_EV_TS 0x0B
  #define M12_EV_TS 0x0C
  #define M13_EV_TS 0x0D
  #define M14_EV_TS 0x0E
  #define M15_EV_TS 0x0F
#define M_WD_TS 0xF0
  #define M1_WD_TS 0x10
  #define M2_WD_TS 0x20
  #define M3_WD_TS 0x30
  #define M4_WD_TS 0x40
  #define M5_WD_TS 0x50
  #define M6_WD_TS 0x60
  #define M7_WD_TS 0x70
  #define M8_WD_TS 0x80
  #define M9_WD_TS 0x90
  #define M10_WD_TS 0xA0
  #define M11_WD_TS 0xB0
  #define M12_WD_TS 0xC0
  #define M13_WD_TS 0xD0
  #define M14_WD_TS 0xE0
  #define M15_WD_TS 0xF0

    typedef struct // bitmap construction
    {
        REGWORD v_ev_ts:4;
        REGWORD v_wd_ts:4;
    } bit_r_ti_wd;

    typedef union {REGWORD reg; bit_r_ti_wd bit;} reg_r_ti_wd; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BERT_WD_MD 0x1B

#define M_PAT_SEQ 0x07
  #define M1_PAT_SEQ 0x01
  #define M2_PAT_SEQ 0x02
  #define M3_PAT_SEQ 0x03
  #define M4_PAT_SEQ 0x04
  #define M5_PAT_SEQ 0x05
  #define M6_PAT_SEQ 0x06
  #define M7_PAT_SEQ 0x07
#define M_BERT_ERR 0x08
#define M_AUTO_WD_RES 0x20
#define M_WD_RES 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_pat_seq:3;
        REGWORD v_bert_err:1;
        REGWORD reserved_54:1;
        REGWORD v_auto_wd_res:1;
        REGWORD reserved_55:1;
        REGWORD v_wd_res:1;
    } bit_r_bert_wd_md;

    typedef union {REGWORD reg; bit_r_bert_wd_md bit;} reg_r_bert_wd_md; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_DTMF0 0x1C

#define M_DTMF_EN 0x01
#define M_HARM_SEL 0x02
#define M_DTMF_RX_CH 0x04
#define M_DTMF_STOP 0x08
#define M_CHBL_SEL 0x10
#define M_RESTART_DTMF 0x40
#define M_ULAW_SEL 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_dtmf_en:1;
        REGWORD v_harm_sel:1;
        REGWORD v_dtmf_rx_ch:1;
        REGWORD v_dtmf_stop:1;
        REGWORD v_chbl_sel:1;
        REGWORD reserved_56:1;
        REGWORD v_restart_dtmf:1;
        REGWORD v_ulaw_sel:1;
    } bit_r_dtmf0;

    typedef union {REGWORD reg; bit_r_dtmf0 bit;} reg_r_dtmf0; // register and bitmap access


//___________________________________________________________________________________//
//                                                                                   //
#define R_DTMF1 0x1D

#define M_DTMF1 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_dtmf1:8;
    } bit_r_dtmf1;

    typedef union {REGWORD reg; bit_r_dtmf1 bit;} reg_r_dtmf1; // register and bitmap access

#define R_PWM0 0x38

#define M_PWM0 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_pwm0:8;
    } bit_r_pwm0;

    typedef union {REGWORD reg; bit_r_pwm0 bit;} reg_r_pwm0; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_PWM1 0x39

#define M_PWM1 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_pwm1:8;
    } bit_r_pwm1;

    typedef union {REGWORD reg; bit_r_pwm1 bit;} reg_r_pwm1; // register and bitmap access

#define R_GPIO_IN0 0x40

#define M_GPIO_IN0 0x01
#define M_GPIO_IN1 0x02
#define M_GPIO_IN2 0x04
#define M_GPIO_IN3 0x08
#define M_GPIO_IN4 0x10
#define M_GPIO_IN5 0x20
#define M_GPIO_IN6 0x40
#define M_GPIO_IN7 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_gpio_in0:1;
        REGWORD v_gpio_in1:1;
        REGWORD v_gpio_in2:1;
        REGWORD v_gpio_in3:1;
        REGWORD v_gpio_in4:1;
        REGWORD v_gpio_in5:1;
        REGWORD v_gpio_in6:1;
        REGWORD v_gpio_in7:1;
    } bit_r_gpio_in0;

    typedef union {REGWORD reg; bit_r_gpio_in0 bit;} reg_r_gpio_in0; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_GPIO_IN1 0x41

#define M_GPIO_IN8 0x01
#define M_GPIO_IN9 0x02
#define M_GPIO_IN10 0x04
#define M_GPIO_IN11 0x08
#define M_GPIO_IN12 0x10
#define M_GPIO_IN13 0x20
#define M_GPIO_IN14 0x40
#define M_GPIO_IN15 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_gpio_in8:1;
        REGWORD v_gpio_in9:1;
        REGWORD v_gpio_in10:1;
        REGWORD v_gpio_in11:1;
        REGWORD v_gpio_in12:1;
        REGWORD v_gpio_in13:1;
        REGWORD v_gpio_in14:1;
        REGWORD v_gpio_in15:1;
    } bit_r_gpio_in1;

    typedef union {REGWORD reg; bit_r_gpio_in1 bit;} reg_r_gpio_in1; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_GPI_IN0 0x44

#define M_GPI_IN0 0x01
#define M_GPI_IN1 0x02
#define M_GPI_IN2 0x04
#define M_GPI_IN3 0x08
#define M_GPI_IN4 0x10
#define M_GPI_IN5 0x20
#define M_GPI_IN6 0x40
#define M_GPI_IN7 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_gpi_in0:1;
        REGWORD v_gpi_in1:1;
        REGWORD v_gpi_in2:1;
        REGWORD v_gpi_in3:1;
        REGWORD v_gpi_in4:1;
        REGWORD v_gpi_in5:1;
        REGWORD v_gpi_in6:1;
        REGWORD v_gpi_in7:1;
    } bit_r_gpi_in0;

    typedef union {REGWORD reg; bit_r_gpi_in0 bit;} reg_r_gpi_in0; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_GPI_IN1 0x45

#define M_GPI_IN8 0x01
#define M_GPI_IN9 0x02
#define M_GPI_IN10 0x04
#define M_GPI_IN11 0x08
#define M_GPI_IN12 0x10
#define M_GPI_IN13 0x20
#define M_GPI_IN14 0x40
#define M_GPI_IN15 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_gpi_in8:1;
        REGWORD v_gpi_in9:1;
        REGWORD v_gpi_in10:1;
        REGWORD v_gpi_in11:1;
        REGWORD v_gpi_in12:1;
        REGWORD v_gpi_in13:1;
        REGWORD v_gpi_in14:1;
        REGWORD v_gpi_in15:1;
    } bit_r_gpi_in1;

    typedef union {REGWORD reg; bit_r_gpi_in1 bit;} reg_r_gpi_in1; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_GPI_IN2 0x46

#define M_GPI_IN16 0x01
#define M_GPI_IN17 0x02
#define M_GPI_IN18 0x04
#define M_GPI_IN19 0x08
#define M_GPI_IN20 0x10
#define M_GPI_IN21 0x20
#define M_GPI_IN22 0x40
#define M_GPI_IN23 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_gpi_in16:1;
        REGWORD v_gpi_in17:1;
        REGWORD v_gpi_in18:1;
        REGWORD v_gpi_in19:1;
        REGWORD v_gpi_in20:1;
        REGWORD v_gpi_in21:1;
        REGWORD v_gpi_in22:1;
        REGWORD v_gpi_in23:1;
    } bit_r_gpi_in2;

    typedef union {REGWORD reg; bit_r_gpi_in2 bit;} reg_r_gpi_in2; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_GPI_IN3 0x47

#define M_GPI_IN24 0x01
#define M_GPI_IN25 0x02
#define M_GPI_IN26 0x04
#define M_GPI_IN27 0x08
#define M_GPI_IN28 0x10
#define M_GPI_IN29 0x20
#define M_GPI_IN30 0x40
#define M_GPI_IN31 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_gpi_in24:1;
        REGWORD v_gpi_in25:1;
        REGWORD v_gpi_in26:1;
        REGWORD v_gpi_in27:1;
        REGWORD v_gpi_in28:1;
        REGWORD v_gpi_in29:1;
        REGWORD v_gpi_in30:1;
        REGWORD v_gpi_in31:1;
    } bit_r_gpi_in3;

    typedef union {REGWORD reg; bit_r_gpi_in3 bit;} reg_r_gpi_in3; // register and bitmap access

#define R_GPIO_OUT0 0x40

#define M_GPIO_OUT0 0x01
#define M_GPIO_OUT1 0x02
#define M_GPIO_OUT2 0x04
#define M_GPIO_OUT3 0x08
#define M_GPIO_OUT4 0x10
#define M_GPIO_OUT5 0x20
#define M_GPIO_OUT6 0x40
#define M_GPIO_OUT7 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_gpio_out0:1;
        REGWORD v_gpio_out1:1;
        REGWORD v_gpio_out2:1;
        REGWORD v_gpio_out3:1;
        REGWORD v_gpio_out4:1;
        REGWORD v_gpio_out5:1;
        REGWORD v_gpio_out6:1;
        REGWORD v_gpio_out7:1;
    } bit_r_gpio_out0;

    typedef union {REGWORD reg; bit_r_gpio_out0 bit;} reg_r_gpio_out0; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_GPIO_OUT1 0x41

#define M_GPIO_OUT8 0x01
#define M_GPIO_OUT9 0x02
#define M_GPIO_OUT10 0x04
#define M_GPIO_OUT11 0x08
#define M_GPIO_OUT12 0x10
#define M_GPIO_OUT13 0x20
#define M_GPIO_OUT14 0x40
#define M_GPIO_OUT15 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_gpio_out8:1;
        REGWORD v_gpio_out9:1;
        REGWORD v_gpio_out10:1;
        REGWORD v_gpio_out11:1;
        REGWORD v_gpio_out12:1;
        REGWORD v_gpio_out13:1;
        REGWORD v_gpio_out14:1;
        REGWORD v_gpio_out15:1;
    } bit_r_gpio_out1;

    typedef union {REGWORD reg; bit_r_gpio_out1 bit;} reg_r_gpio_out1; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_GPIO_EN0 0x42

#define M_GPIO_EN0 0x01
#define M_GPIO_EN1 0x02
#define M_GPIO_EN2 0x04
#define M_GPIO_EN3 0x08
#define M_GPIO_EN4 0x10
#define M_GPIO_EN5 0x20
#define M_GPIO_EN6 0x40
#define M_GPIO_EN7 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_gpio_en0:1;
        REGWORD v_gpio_en1:1;
        REGWORD v_gpio_en2:1;
        REGWORD v_gpio_en3:1;
        REGWORD v_gpio_en4:1;
        REGWORD v_gpio_en5:1;
        REGWORD v_gpio_en6:1;
        REGWORD v_gpio_en7:1;
    } bit_r_gpio_en0;

    typedef union {REGWORD reg; bit_r_gpio_en0 bit;} reg_r_gpio_en0; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_GPIO_EN1 0x43

#define M_GPIO_EN8 0x01
#define M_GPIO_EN9 0x02
#define M_GPIO_EN10 0x04
#define M_GPIO_EN11 0x08
#define M_GPIO_EN12 0x10
#define M_GPIO_EN13 0x20
#define M_GPIO_EN14 0x40
#define M_GPIO_EN15 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_gpio_en8:1;
        REGWORD v_gpio_en9:1;
        REGWORD v_gpio_en10:1;
        REGWORD v_gpio_en11:1;
        REGWORD v_gpio_en12:1;
        REGWORD v_gpio_en13:1;
        REGWORD v_gpio_en14:1;
        REGWORD v_gpio_en15:1;
    } bit_r_gpio_en1;

    typedef union {REGWORD reg; bit_r_gpio_en1 bit;} reg_r_gpio_en1; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_GPIO_SEL 0x44

#define M_GPIO_SEL0 0x01
#define M_GPIO_SEL1 0x02
#define M_GPIO_SEL2 0x04
#define M_GPIO_SEL3 0x08
#define M_GPIO_SEL4 0x10
#define M_GPIO_SEL5 0x20
#define M_GPIO_SEL6 0x40
#define M_GPIO_SEL7 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_gpio_sel0:1;
        REGWORD v_gpio_sel1:1;
        REGWORD v_gpio_sel2:1;
        REGWORD v_gpio_sel3:1;
        REGWORD v_gpio_sel4:1;
        REGWORD v_gpio_sel5:1;
        REGWORD v_gpio_sel6:1;
        REGWORD v_gpio_sel7:1;
    } bit_r_gpio_sel;

    typedef union {REGWORD reg; bit_r_gpio_sel bit;} reg_r_gpio_sel; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BRG_CTRL 0x45

#define M_BRG_CS 0x07
  #define M1_BRG_CS 0x01
  #define M2_BRG_CS 0x02
  #define M3_BRG_CS 0x03
  #define M4_BRG_CS 0x04
  #define M5_BRG_CS 0x05
  #define M6_BRG_CS 0x06
  #define M7_BRG_CS 0x07
#define M_BRG_ADDR 0x18
  #define M1_BRG_ADDR 0x08
  #define M2_BRG_ADDR 0x10
  #define M3_BRG_ADDR 0x18
#define M_BRG_CS_SRC 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_brg_cs:3;
        REGWORD v_brg_addr:2;
        REGWORD reserved_57:2;
        REGWORD v_brg_cs_src:1;
    } bit_r_brg_ctrl;

    typedef union {REGWORD reg; bit_r_brg_ctrl bit;} reg_r_brg_ctrl; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_PWM_MD 0x46

#define M_EXT_IRQ_EN 0x08
#define M_PWM0_MD 0x30
  #define M1_PWM0_MD 0x10
  #define M2_PWM0_MD 0x20
  #define M3_PWM0_MD 0x30
#define M_PWM1_MD 0xC0
  #define M1_PWM1_MD 0x40
  #define M2_PWM1_MD 0x80
  #define M3_PWM1_MD 0xC0

    typedef struct // bitmap construction
    {
        REGWORD reserved_58:3;
        REGWORD v_ext_irq_en:1;
        REGWORD v_pwm0_md:2;
        REGWORD v_pwm1_md:2;
    } bit_r_pwm_md;

    typedef union {REGWORD reg; bit_r_pwm_md bit;} reg_r_pwm_md; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BRG_MD 0x47

#define M_BRG_MD0 0x01
#define M_BRG_MD1 0x02
#define M_BRG_MD2 0x04
#define M_BRG_MD3 0x08
#define M_BRG_MD4 0x10
#define M_BRG_MD5 0x20
#define M_BRG_MD6 0x40
#define M_BRG_MD7 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_brg_md0:1;
        REGWORD v_brg_md1:1;
        REGWORD v_brg_md2:1;
        REGWORD v_brg_md3:1;
        REGWORD v_brg_md4:1;
        REGWORD v_brg_md5:1;
        REGWORD v_brg_md6:1;
        REGWORD v_brg_md7:1;
    } bit_r_brg_md;

    typedef union {REGWORD reg; bit_r_brg_md bit;} reg_r_brg_md; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BRG_TIM0 0x48

#define M_BRG_TIM0_IDLE 0x0F
  #define M1_BRG_TIM0_IDLE 0x01
  #define M2_BRG_TIM0_IDLE 0x02
  #define M3_BRG_TIM0_IDLE 0x03
  #define M4_BRG_TIM0_IDLE 0x04
  #define M5_BRG_TIM0_IDLE 0x05
  #define M6_BRG_TIM0_IDLE 0x06
  #define M7_BRG_TIM0_IDLE 0x07
  #define M8_BRG_TIM0_IDLE 0x08
  #define M9_BRG_TIM0_IDLE 0x09
  #define M10_BRG_TIM0_IDLE 0x0A
  #define M11_BRG_TIM0_IDLE 0x0B
  #define M12_BRG_TIM0_IDLE 0x0C
  #define M13_BRG_TIM0_IDLE 0x0D
  #define M14_BRG_TIM0_IDLE 0x0E
  #define M15_BRG_TIM0_IDLE 0x0F
#define M_BRG_TIM0_CLK 0xF0
  #define M1_BRG_TIM0_CLK 0x10
  #define M2_BRG_TIM0_CLK 0x20
  #define M3_BRG_TIM0_CLK 0x30
  #define M4_BRG_TIM0_CLK 0x40
  #define M5_BRG_TIM0_CLK 0x50
  #define M6_BRG_TIM0_CLK 0x60
  #define M7_BRG_TIM0_CLK 0x70
  #define M8_BRG_TIM0_CLK 0x80
  #define M9_BRG_TIM0_CLK 0x90
  #define M10_BRG_TIM0_CLK 0xA0
  #define M11_BRG_TIM0_CLK 0xB0
  #define M12_BRG_TIM0_CLK 0xC0
  #define M13_BRG_TIM0_CLK 0xD0
  #define M14_BRG_TIM0_CLK 0xE0
  #define M15_BRG_TIM0_CLK 0xF0

    typedef struct // bitmap construction
    {
        REGWORD v_brg_tim0_idle:4;
        REGWORD v_brg_tim0_clk:4;
    } bit_r_brg_tim0;

    typedef union {REGWORD reg; bit_r_brg_tim0 bit;} reg_r_brg_tim0; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BRG_TIM1 0x49

#define M_BRG_TIM1_IDLE 0x0F
  #define M1_BRG_TIM1_IDLE 0x01
  #define M2_BRG_TIM1_IDLE 0x02
  #define M3_BRG_TIM1_IDLE 0x03
  #define M4_BRG_TIM1_IDLE 0x04
  #define M5_BRG_TIM1_IDLE 0x05
  #define M6_BRG_TIM1_IDLE 0x06
  #define M7_BRG_TIM1_IDLE 0x07
  #define M8_BRG_TIM1_IDLE 0x08
  #define M9_BRG_TIM1_IDLE 0x09
  #define M10_BRG_TIM1_IDLE 0x0A
  #define M11_BRG_TIM1_IDLE 0x0B
  #define M12_BRG_TIM1_IDLE 0x0C
  #define M13_BRG_TIM1_IDLE 0x0D
  #define M14_BRG_TIM1_IDLE 0x0E
  #define M15_BRG_TIM1_IDLE 0x0F
#define M_BRG_TIM1_CLK 0xF0
  #define M1_BRG_TIM1_CLK 0x10
  #define M2_BRG_TIM1_CLK 0x20
  #define M3_BRG_TIM1_CLK 0x30
  #define M4_BRG_TIM1_CLK 0x40
  #define M5_BRG_TIM1_CLK 0x50
  #define M6_BRG_TIM1_CLK 0x60
  #define M7_BRG_TIM1_CLK 0x70
  #define M8_BRG_TIM1_CLK 0x80
  #define M9_BRG_TIM1_CLK 0x90
  #define M10_BRG_TIM1_CLK 0xA0
  #define M11_BRG_TIM1_CLK 0xB0
  #define M12_BRG_TIM1_CLK 0xC0
  #define M13_BRG_TIM1_CLK 0xD0
  #define M14_BRG_TIM1_CLK 0xE0
  #define M15_BRG_TIM1_CLK 0xF0

    typedef struct // bitmap construction
    {
        REGWORD v_brg_tim1_idle:4;
        REGWORD v_brg_tim1_clk:4;
    } bit_r_brg_tim1;

    typedef union {REGWORD reg; bit_r_brg_tim1 bit;} reg_r_brg_tim1; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BRG_TIM2 0x4A

#define M_BRG_TIM2_IDLE 0x0F
  #define M1_BRG_TIM2_IDLE 0x01
  #define M2_BRG_TIM2_IDLE 0x02
  #define M3_BRG_TIM2_IDLE 0x03
  #define M4_BRG_TIM2_IDLE 0x04
  #define M5_BRG_TIM2_IDLE 0x05
  #define M6_BRG_TIM2_IDLE 0x06
  #define M7_BRG_TIM2_IDLE 0x07
  #define M8_BRG_TIM2_IDLE 0x08
  #define M9_BRG_TIM2_IDLE 0x09
  #define M10_BRG_TIM2_IDLE 0x0A
  #define M11_BRG_TIM2_IDLE 0x0B
  #define M12_BRG_TIM2_IDLE 0x0C
  #define M13_BRG_TIM2_IDLE 0x0D
  #define M14_BRG_TIM2_IDLE 0x0E
  #define M15_BRG_TIM2_IDLE 0x0F
#define M_BRG_TIM2_CLK 0xF0
  #define M1_BRG_TIM2_CLK 0x10
  #define M2_BRG_TIM2_CLK 0x20
  #define M3_BRG_TIM2_CLK 0x30
  #define M4_BRG_TIM2_CLK 0x40
  #define M5_BRG_TIM2_CLK 0x50
  #define M6_BRG_TIM2_CLK 0x60
  #define M7_BRG_TIM2_CLK 0x70
  #define M8_BRG_TIM2_CLK 0x80
  #define M9_BRG_TIM2_CLK 0x90
  #define M10_BRG_TIM2_CLK 0xA0
  #define M11_BRG_TIM2_CLK 0xB0
  #define M12_BRG_TIM2_CLK 0xC0
  #define M13_BRG_TIM2_CLK 0xD0
  #define M14_BRG_TIM2_CLK 0xE0
  #define M15_BRG_TIM2_CLK 0xF0

    typedef struct // bitmap construction
    {
        REGWORD v_brg_tim2_idle:4;
        REGWORD v_brg_tim2_clk:4;
    } bit_r_brg_tim2;

    typedef union {REGWORD reg; bit_r_brg_tim2 bit;} reg_r_brg_tim2; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BRG_TIM3 0x4B

#define M_BRG_TIM3_IDLE 0x0F
  #define M1_BRG_TIM3_IDLE 0x01
  #define M2_BRG_TIM3_IDLE 0x02
  #define M3_BRG_TIM3_IDLE 0x03
  #define M4_BRG_TIM3_IDLE 0x04
  #define M5_BRG_TIM3_IDLE 0x05
  #define M6_BRG_TIM3_IDLE 0x06
  #define M7_BRG_TIM3_IDLE 0x07
  #define M8_BRG_TIM3_IDLE 0x08
  #define M9_BRG_TIM3_IDLE 0x09
  #define M10_BRG_TIM3_IDLE 0x0A
  #define M11_BRG_TIM3_IDLE 0x0B
  #define M12_BRG_TIM3_IDLE 0x0C
  #define M13_BRG_TIM3_IDLE 0x0D
  #define M14_BRG_TIM3_IDLE 0x0E
  #define M15_BRG_TIM3_IDLE 0x0F
#define M_BRG_TIM3_CLK 0xF0
  #define M1_BRG_TIM3_CLK 0x10
  #define M2_BRG_TIM3_CLK 0x20
  #define M3_BRG_TIM3_CLK 0x30
  #define M4_BRG_TIM3_CLK 0x40
  #define M5_BRG_TIM3_CLK 0x50
  #define M6_BRG_TIM3_CLK 0x60
  #define M7_BRG_TIM3_CLK 0x70
  #define M8_BRG_TIM3_CLK 0x80
  #define M9_BRG_TIM3_CLK 0x90
  #define M10_BRG_TIM3_CLK 0xA0
  #define M11_BRG_TIM3_CLK 0xB0
  #define M12_BRG_TIM3_CLK 0xC0
  #define M13_BRG_TIM3_CLK 0xD0
  #define M14_BRG_TIM3_CLK 0xE0
  #define M15_BRG_TIM3_CLK 0xF0

    typedef struct // bitmap construction
    {
        REGWORD v_brg_tim3_idle:4;
        REGWORD v_brg_tim3_clk:4;
    } bit_r_brg_tim3;

    typedef union {REGWORD reg; bit_r_brg_tim3 bit;} reg_r_brg_tim3; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BRG_TIM_SEL01 0x4C

#define M_BRG_WR_SEL0 0x03
  #define M1_BRG_WR_SEL0 0x01
  #define M2_BRG_WR_SEL0 0x02
  #define M3_BRG_WR_SEL0 0x03
#define M_BRG_RD_SEL0 0x0C
  #define M1_BRG_RD_SEL0 0x04
  #define M2_BRG_RD_SEL0 0x08
  #define M3_BRG_RD_SEL0 0x0C
#define M_BRG_WR_SEL1 0x30
  #define M1_BRG_WR_SEL1 0x10
  #define M2_BRG_WR_SEL1 0x20
  #define M3_BRG_WR_SEL1 0x30
#define M_BRG_RD_SEL1 0xC0
  #define M1_BRG_RD_SEL1 0x40
  #define M2_BRG_RD_SEL1 0x80
  #define M3_BRG_RD_SEL1 0xC0

    typedef struct // bitmap construction
    {
        REGWORD v_brg_wr_sel0:2;
        REGWORD v_brg_rd_sel0:2;
        REGWORD v_brg_wr_sel1:2;
        REGWORD v_brg_rd_sel1:2;
    } bit_r_brg_tim_sel01;

    typedef union {REGWORD reg; bit_r_brg_tim_sel01 bit;} reg_r_brg_tim_sel01; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BRG_TIM_SEL23 0x4D

#define M_BRG_WR_SEL2 0x03
  #define M1_BRG_WR_SEL2 0x01
  #define M2_BRG_WR_SEL2 0x02
  #define M3_BRG_WR_SEL2 0x03
#define M_BRG_RD_SEL2 0x0C
  #define M1_BRG_RD_SEL2 0x04
  #define M2_BRG_RD_SEL2 0x08
  #define M3_BRG_RD_SEL2 0x0C
#define M_BRG_WR_SEL3 0x30
  #define M1_BRG_WR_SEL3 0x10
  #define M2_BRG_WR_SEL3 0x20
  #define M3_BRG_WR_SEL3 0x30
#define M_BRG_RD_SEL3 0xC0
  #define M1_BRG_RD_SEL3 0x40
  #define M2_BRG_RD_SEL3 0x80
  #define M3_BRG_RD_SEL3 0xC0

    typedef struct // bitmap construction
    {
        REGWORD v_brg_wr_sel2:2;
        REGWORD v_brg_rd_sel2:2;
        REGWORD v_brg_wr_sel3:2;
        REGWORD v_brg_rd_sel3:2;
    } bit_r_brg_tim_sel23;

    typedef union {REGWORD reg; bit_r_brg_tim_sel23 bit;} reg_r_brg_tim_sel23; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BRG_TIM_SEL45 0x4E

#define M_BRG_WR_SEL4 0x03
  #define M1_BRG_WR_SEL4 0x01
  #define M2_BRG_WR_SEL4 0x02
  #define M3_BRG_WR_SEL4 0x03
#define M_BRG_RD_SEL4 0x0C
  #define M1_BRG_RD_SEL4 0x04
  #define M2_BRG_RD_SEL4 0x08
  #define M3_BRG_RD_SEL4 0x0C
#define M_BRG_WR_SEL5 0x30
  #define M1_BRG_WR_SEL5 0x10
  #define M2_BRG_WR_SEL5 0x20
  #define M3_BRG_WR_SEL5 0x30
#define M_BRG_RD_SEL5 0xC0
  #define M1_BRG_RD_SEL5 0x40
  #define M2_BRG_RD_SEL5 0x80
  #define M3_BRG_RD_SEL5 0xC0

    typedef struct // bitmap construction
    {
        REGWORD v_brg_wr_sel4:2;
        REGWORD v_brg_rd_sel4:2;
        REGWORD v_brg_wr_sel5:2;
        REGWORD v_brg_rd_sel5:2;
    } bit_r_brg_tim_sel45;

    typedef union {REGWORD reg; bit_r_brg_tim_sel45 bit;} reg_r_brg_tim_sel45; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_BRG_TIM_SEL67 0x4F

#define M_BRG_WR_SEL6 0x03
  #define M1_BRG_WR_SEL6 0x01
  #define M2_BRG_WR_SEL6 0x02
  #define M3_BRG_WR_SEL6 0x03
#define M_BRG_RD_SEL6 0x0C
  #define M1_BRG_RD_SEL6 0x04
  #define M2_BRG_RD_SEL6 0x08
  #define M3_BRG_RD_SEL6 0x0C
#define M_BRG_WR_SEL7 0x30
  #define M1_BRG_WR_SEL7 0x10
  #define M2_BRG_WR_SEL7 0x20
  #define M3_BRG_WR_SEL7 0x30
#define M_BRG_RD_SEL7 0xC0
  #define M1_BRG_RD_SEL7 0x40
  #define M2_BRG_RD_SEL7 0x80
  #define M3_BRG_RD_SEL7 0xC0

    typedef struct // bitmap construction
    {
        REGWORD v_brg_wr_sel6:2;
        REGWORD v_brg_rd_sel6:2;
        REGWORD v_brg_wr_sel7:2;
        REGWORD v_brg_rd_sel7:2;
    } bit_r_brg_tim_sel67;

    typedef union {REGWORD reg; bit_r_brg_tim_sel67 bit;} reg_r_brg_tim_sel67; // register and bitmap access

#define A_FIFO_DATA0 0x80

#define M_FIFO_DATA0 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_fifo_data0:8;
    } bit_a_fifo_data0;

    typedef union {REGWORD reg; bit_a_fifo_data0 bit;} reg_a_fifo_data0; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_FIFO_DATA1 0x81

#define M_FIFO_DATA1 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_fifo_data1:8;
    } bit_a_fifo_data1;

    typedef union {REGWORD reg; bit_a_fifo_data1 bit;} reg_a_fifo_data1; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_FIFO_DATA2 0x82

#define M_FIFO_DATA2 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_fifo_data2:8;
    } bit_a_fifo_data2;

    typedef union {REGWORD reg; bit_a_fifo_data2 bit;} reg_a_fifo_data2; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_FIFO_DATA3 0x83

#define M_FIFO_DATA3 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_fifo_data3:8;
    } bit_a_fifo_data3;

    typedef union {REGWORD reg; bit_a_fifo_data3 bit;} reg_a_fifo_data3; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_FIFO_DATA0_NOINC 0x84

#define M_FIFO_DATA0_NOINC 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_fifo_data0_noinc:8;
    } bit_a_fifo_data0_noinc;

    typedef union {REGWORD reg; bit_a_fifo_data0_noinc bit;} reg_a_fifo_data0_noinc; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_FIFO_DATA1_NOINC 0x85

#define M_FIFO_DATA_NOINC1 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_fifo_data_noinc1:8;
    } bit_a_fifo_data1_noinc;

    typedef union {REGWORD reg; bit_a_fifo_data1_noinc bit;} reg_a_fifo_data1_noinc; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_FIFO_DATA2_NOINC 0x86

#define M_FIFO_DATA2_NOINC 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_fifo_data2_noinc:8;
    } bit_a_fifo_data2_noinc;

    typedef union {REGWORD reg; bit_a_fifo_data2_noinc bit;} reg_a_fifo_data2_noinc; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_FIFO_DATA3_NOINC 0x87

#define M_FIFO_DATA3_NOINC 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_fifo_data3_noinc:8;
    } bit_a_fifo_data3_noinc;

    typedef union {REGWORD reg; bit_a_fifo_data3_noinc bit;} reg_a_fifo_data3_noinc; // register and bitmap access

#define R_RAM_DATA 0xC0

#define M_RAM_DATA 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_ram_data:8;
    } bit_r_ram_data;

    typedef union {REGWORD reg; bit_r_ram_data bit;} reg_r_ram_data; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_IRQ_FIFO_BL0 0xC8

#define M_IRQ_FIFO0_TX 0x01
#define M_IRQ_FIFO0_RX 0x02
#define M_IRQ_FIFO1_TX 0x04
#define M_IRQ_FIFO1_RX 0x08
#define M_IRQ_FIFO2_TX 0x10
#define M_IRQ_FIFO2_RX 0x20
#define M_IRQ_FIFO3_TX 0x40
#define M_IRQ_FIFO3_RX 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_irq_fifo0_tx:1;
        REGWORD v_irq_fifo0_rx:1;
        REGWORD v_irq_fifo1_tx:1;
        REGWORD v_irq_fifo1_rx:1;
        REGWORD v_irq_fifo2_tx:1;
        REGWORD v_irq_fifo2_rx:1;
        REGWORD v_irq_fifo3_tx:1;
        REGWORD v_irq_fifo3_rx:1;
    } bit_r_irq_fifo_bl0;

    typedef union {REGWORD reg; bit_r_irq_fifo_bl0 bit;} reg_r_irq_fifo_bl0; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_IRQ_FIFO_BL1 0xC9

#define M_IRQ_FIFO4_TX 0x01
#define M_IRQ_FIFO4_RX 0x02
#define M_IRQ_FIFO5_TX 0x04
#define M_IRQ_FIFO5_RX 0x08
#define M_IRQ_FIFO6_TX 0x10
#define M_IRQ_FIFO6_RX 0x20
#define M_IRQ_FIFO7_TX 0x40
#define M_IRQ_FIFO7_RX 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_irq_fifo4_tx:1;
        REGWORD v_irq_fifo4_rx:1;
        REGWORD v_irq_fifo5_tx:1;
        REGWORD v_irq_fifo5_rx:1;
        REGWORD v_irq_fifo6_tx:1;
        REGWORD v_irq_fifo6_rx:1;
        REGWORD v_irq_fifo7_tx:1;
        REGWORD v_irq_fifo7_rx:1;
    } bit_r_irq_fifo_bl1;

    typedef union {REGWORD reg; bit_r_irq_fifo_bl1 bit;} reg_r_irq_fifo_bl1; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_IRQ_FIFO_BL2 0xCA

#define M_IRQ_FIFO8_TX 0x01
#define M_IRQ_FIFO8_RX 0x02
#define M_IRQ_FIFO9_TX 0x04
#define M_IRQ_FIFO9_RX 0x08
#define M_IRQ_FIFO10_TX 0x10
#define M_IRQ_FIFO10_RX 0x20
#define M_IRQ_FIFO11_TX 0x40
#define M_IRQ_FIFO11_RX 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_irq_fifo8_tx:1;
        REGWORD v_irq_fifo8_rx:1;
        REGWORD v_irq_fifo9_tx:1;
        REGWORD v_irq_fifo9_rx:1;
        REGWORD v_irq_fifo10_tx:1;
        REGWORD v_irq_fifo10_rx:1;
        REGWORD v_irq_fifo11_tx:1;
        REGWORD v_irq_fifo11_rx:1;
    } bit_r_irq_fifo_bl2;

    typedef union {REGWORD reg; bit_r_irq_fifo_bl2 bit;} reg_r_irq_fifo_bl2; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_IRQ_FIFO_BL3 0xCB

#define M_IRQ_FIFO12_TX 0x01
#define M_IRQ_FIFO12_RX 0x02
#define M_IRQ_FIFO13_TX 0x04
#define M_IRQ_FIFO13_RX 0x08
#define M_IRQ_FIFO14_TX 0x10
#define M_IRQ_FIFO14_RX 0x20
#define M_IRQ_FIFO15_TX 0x40
#define M_IRQ_FIFO15_RX 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_irq_fifo12_tx:1;
        REGWORD v_irq_fifo12_rx:1;
        REGWORD v_irq_fifo13_tx:1;
        REGWORD v_irq_fifo13_rx:1;
        REGWORD v_irq_fifo14_tx:1;
        REGWORD v_irq_fifo14_rx:1;
        REGWORD v_irq_fifo15_tx:1;
        REGWORD v_irq_fifo15_rx:1;
    } bit_r_irq_fifo_bl3;

    typedef union {REGWORD reg; bit_r_irq_fifo_bl3 bit;} reg_r_irq_fifo_bl3; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_IRQ_FIFO_BL4 0xCC

#define M_IRQ_FIFO16_TX 0x01
#define M_IRQ_FIFO16_RX 0x02
#define M_IRQ_FIFO17_TX 0x04
#define M_IRQ_FIFO17_RX 0x08
#define M_IRQ_FIFO18_TX 0x10
#define M_IRQ_FIFO18_RX 0x20
#define M_IRQ_FIFO19_TX 0x40
#define M_IRQ_FIFO19_RX 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_irq_fifo16_tx:1;
        REGWORD v_irq_fifo16_rx:1;
        REGWORD v_irq_fifo17_tx:1;
        REGWORD v_irq_fifo17_rx:1;
        REGWORD v_irq_fifo18_tx:1;
        REGWORD v_irq_fifo18_rx:1;
        REGWORD v_irq_fifo19_tx:1;
        REGWORD v_irq_fifo19_rx:1;
    } bit_r_irq_fifo_bl4;

    typedef union {REGWORD reg; bit_r_irq_fifo_bl4 bit;} reg_r_irq_fifo_bl4; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_IRQ_FIFO_BL5 0xCD

#define M_IRQ_FIFO20_TX 0x01
#define M_IRQ_FIFO20_RX 0x02
#define M_IRQ_FIFO21_TX 0x04
#define M_IRQ_FIFO21_RX 0x08
#define M_IRQ_FIFO22_TX 0x10
#define M_IRQ_FIFO22_RX 0x20
#define M_IRQ_FIFO23_TX 0x40
#define M_IRQ_FIFO23_RX 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_irq_fifo20_tx:1;
        REGWORD v_irq_fifo20_rx:1;
        REGWORD v_irq_fifo21_tx:1;
        REGWORD v_irq_fifo21_rx:1;
        REGWORD v_irq_fifo22_tx:1;
        REGWORD v_irq_fifo22_rx:1;
        REGWORD v_irq_fifo23_tx:1;
        REGWORD v_irq_fifo23_rx:1;
    } bit_r_irq_fifo_bl5;

    typedef union {REGWORD reg; bit_r_irq_fifo_bl5 bit;} reg_r_irq_fifo_bl5; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_IRQ_FIFO_BL6 0xCE

#define M_IRQ_FIFO24_TX 0x01
#define M_IRQ_FIFO24_RX 0x02
#define M_IRQ_FIFO25_TX 0x04
#define M_IRQ_FIFO25_RX 0x08
#define M_IRQ_FIFO26_TX 0x10
#define M_IRQ_FIFO26_RX 0x20
#define M_IRQ_FIFO27_TX 0x40
#define M_IRQ_FIFO27_RX 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_irq_fifo24_tx:1;
        REGWORD v_irq_fifo24_rx:1;
        REGWORD v_irq_fifo25_tx:1;
        REGWORD v_irq_fifo25_rx:1;
        REGWORD v_irq_fifo26_tx:1;
        REGWORD v_irq_fifo26_rx:1;
        REGWORD v_irq_fifo27_tx:1;
        REGWORD v_irq_fifo27_rx:1;
    } bit_r_irq_fifo_bl6;

    typedef union {REGWORD reg; bit_r_irq_fifo_bl6 bit;} reg_r_irq_fifo_bl6; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define R_IRQ_FIFO_BL7 0xCF

#define M_IRQ_FIFO28_TX 0x01
#define M_IRQ_FIFO28_RX 0x02
#define M_IRQ_FIFO29_TX 0x04
#define M_IRQ_FIFO29_RX 0x08
#define M_IRQ_FIFO30_TX 0x10
#define M_IRQ_FIFO30_RX 0x20
#define M_IRQ_FIFO31_TX 0x40
#define M_IRQ_FIFO31_RX 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_irq_fifo28_tx:1;
        REGWORD v_irq_fifo28_rx:1;
        REGWORD v_irq_fifo29_tx:1;
        REGWORD v_irq_fifo29_rx:1;
        REGWORD v_irq_fifo30_tx:1;
        REGWORD v_irq_fifo30_rx:1;
        REGWORD v_irq_fifo31_tx:1;
        REGWORD v_irq_fifo31_rx:1;
    } bit_r_irq_fifo_bl7;

    typedef union {REGWORD reg; bit_r_irq_fifo_bl7 bit;} reg_r_irq_fifo_bl7; // register and bitmap access

#define A_SL_CFG 0xD0

#define M_CH_DIR 0x01
#define M_CH1_SEL 0x3E
  #define M1_CH1_SEL 0x02
  #define M2_CH1_SEL 0x04
  #define M3_CH1_SEL 0x06
  #define M4_CH1_SEL 0x08
  #define M5_CH1_SEL 0x0A
  #define M6_CH1_SEL 0x0C
  #define M7_CH1_SEL 0x0E
  #define M8_CH1_SEL 0x10
  #define M9_CH1_SEL 0x12
  #define M10_CH1_SEL 0x14
  #define M11_CH1_SEL 0x16
  #define M12_CH1_SEL 0x18
  #define M13_CH1_SEL 0x1A
  #define M14_CH1_SEL 0x1C
  #define M15_CH1_SEL 0x1E
  #define M16_CH1_SEL 0x20
  #define M17_CH1_SEL 0x22
  #define M18_CH1_SEL 0x24
  #define M19_CH1_SEL 0x26
  #define M20_CH1_SEL 0x28
  #define M21_CH1_SEL 0x2A
  #define M22_CH1_SEL 0x2C
  #define M23_CH1_SEL 0x2E
  #define M24_CH1_SEL 0x30
  #define M25_CH1_SEL 0x32
  #define M26_CH1_SEL 0x34
  #define M27_CH1_SEL 0x36
  #define M28_CH1_SEL 0x38
  #define M29_CH1_SEL 0x3A
  #define M30_CH1_SEL 0x3C
  #define M31_CH1_SEL 0x3E
#define M_ROUT 0xC0
  #define M1_ROUT 0x40
  #define M2_ROUT 0x80
  #define M3_ROUT 0xC0

    typedef struct // bitmap construction
    {
        REGWORD v_ch_dir:1;
        REGWORD v_ch1_sel:5;
        REGWORD v_rout:2;
    } bit_a_sl_cfg;

    typedef union {REGWORD reg; bit_a_sl_cfg bit;} reg_a_sl_cfg; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_CONF 0xD1

#define M_CONF_NUM 0x07
  #define M1_CONF_NUM 0x01
  #define M2_CONF_NUM 0x02
  #define M3_CONF_NUM 0x03
  #define M4_CONF_NUM 0x04
  #define M5_CONF_NUM 0x05
  #define M6_CONF_NUM 0x06
  #define M7_CONF_NUM 0x07
#define M_NOISE_SUPPR 0x18
  #define M1_NOISE_SUPPR 0x08
  #define M2_NOISE_SUPPR 0x10
  #define M3_NOISE_SUPPR 0x18
#define M_ATT_LEV 0x60
  #define M1_ATT_LEV 0x20
  #define M2_ATT_LEV 0x40
  #define M3_ATT_LEV 0x60
#define M_CONF_SL 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_conf_num:3;
        REGWORD v_noise_suppr:2;
        REGWORD v_att_lev:2;
        REGWORD v_conf_sl:1;
    } bit_a_conf;

    typedef union {REGWORD reg; bit_a_conf bit;} reg_a_conf; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_CH_MSK 0xF4

#define M_CH_MSK 0xFF

    typedef struct // bitmap construction
    {
        REGWORD v_ch_msk:8;
    } bit_a_ch_msk;

    typedef union {REGWORD reg; bit_a_ch_msk bit;} reg_a_ch_msk; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_CON_HDLC 0xFA

#define M_IFF 0x01
#define M_HDLC_TRP 0x02
#define M_TRP_IRQ 0x1C
  #define M1_TRP_IRQ 0x04
  #define M2_TRP_IRQ 0x08
  #define M3_TRP_IRQ 0x0C
  #define M4_TRP_IRQ 0x10
  #define M5_TRP_IRQ 0x14
  #define M6_TRP_IRQ 0x18
  #define M7_TRP_IRQ 0x1C
#define M_DATA_FLOW 0xE0
  #define M1_DATA_FLOW 0x20
  #define M2_DATA_FLOW 0x40
  #define M3_DATA_FLOW 0x60
  #define M4_DATA_FLOW 0x80
  #define M5_DATA_FLOW 0xA0
  #define M6_DATA_FLOW 0xC0
  #define M7_DATA_FLOW 0xE0

    typedef struct // bitmap construction
    {
        REGWORD v_iff:1;
        REGWORD v_hdlc_trp:1;
        REGWORD v_trp_irq:3;
        REGWORD v_data_flow:3;
    } bit_a_con_hdlc;

    typedef union {REGWORD reg; bit_a_con_hdlc bit;} reg_a_con_hdlc; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_SUBCH_CFG 0xFB

#define M_BIT_CNT 0x07
  #define M1_BIT_CNT 0x01
  #define M2_BIT_CNT 0x02
  #define M3_BIT_CNT 0x03
  #define M4_BIT_CNT 0x04
  #define M5_BIT_CNT 0x05
  #define M6_BIT_CNT 0x06
  #define M7_BIT_CNT 0x07
#define M_START_BIT 0x38
  #define M1_START_BIT 0x08
  #define M2_START_BIT 0x10
  #define M3_START_BIT 0x18
  #define M4_START_BIT 0x20
  #define M5_START_BIT 0x28
  #define M6_START_BIT 0x30
  #define M7_START_BIT 0x38
#define M_LOOP_FIFO 0x40
#define M_INV_DATA 0x80

    typedef struct // bitmap construction
    {
        REGWORD v_bit_cnt:3;
        REGWORD v_start_bit:3;
        REGWORD v_loop_fifo:1;
        REGWORD v_inv_data:1;
    } bit_a_subch_cfg;

    typedef union {REGWORD reg; bit_a_subch_cfg bit;} reg_a_subch_cfg; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_FIFO_SEQ 0xFD

#define M_NEXT_FIFO_DIR 0x01
#define M_NEXT_FIFO_NUM 0x3E
  #define M1_NEXT_FIFO_NUM 0x02
  #define M2_NEXT_FIFO_NUM 0x04
  #define M3_NEXT_FIFO_NUM 0x06
  #define M4_NEXT_FIFO_NUM 0x08
  #define M5_NEXT_FIFO_NUM 0x0A
  #define M6_NEXT_FIFO_NUM 0x0C
  #define M7_NEXT_FIFO_NUM 0x0E
  #define M8_NEXT_FIFO_NUM 0x10
  #define M9_NEXT_FIFO_NUM 0x12
  #define M10_NEXT_FIFO_NUM 0x14
  #define M11_NEXT_FIFO_NUM 0x16
  #define M12_NEXT_FIFO_NUM 0x18
  #define M13_NEXT_FIFO_NUM 0x1A
  #define M14_NEXT_FIFO_NUM 0x1C
  #define M15_NEXT_FIFO_NUM 0x1E
  #define M16_NEXT_FIFO_NUM 0x20
  #define M17_NEXT_FIFO_NUM 0x22
  #define M18_NEXT_FIFO_NUM 0x24
  #define M19_NEXT_FIFO_NUM 0x26
  #define M20_NEXT_FIFO_NUM 0x28
  #define M21_NEXT_FIFO_NUM 0x2A
  #define M22_NEXT_FIFO_NUM 0x2C
  #define M23_NEXT_FIFO_NUM 0x2E
  #define M24_NEXT_FIFO_NUM 0x30
  #define M25_NEXT_FIFO_NUM 0x32
  #define M26_NEXT_FIFO_NUM 0x34
  #define M27_NEXT_FIFO_NUM 0x36
  #define M28_NEXT_FIFO_NUM 0x38
  #define M29_NEXT_FIFO_NUM 0x3A
  #define M30_NEXT_FIFO_NUM 0x3C
  #define M31_NEXT_FIFO_NUM 0x3E
#define M_SEQ_END 0x40

    typedef struct // bitmap construction
    {
        REGWORD v_next_fifo_dir:1;
        REGWORD v_next_fifo_num:5;
        REGWORD v_seq_end:1;
        REGWORD reserved_59:1;
    } bit_a_fifo_seq;

    typedef union {REGWORD reg; bit_a_fifo_seq bit;} reg_a_fifo_seq; // register and bitmap access

//___________________________________________________________________________________//
//                                                                                   //
#define A_IRQ_MSK 0xFF

#define M_IRQ 0x01
#define M_BERT_EN 0x02
#define M_MIX_IRQ 0x04

    typedef struct // bitmap construction
    {
        REGWORD v_irq:1;
        REGWORD v_bert_en:1;
        REGWORD v_mix_irq:1;
        REGWORD reserved_60:5;
    } bit_a_irq_msk;

    typedef union {REGWORD reg; bit_a_irq_msk bit;} reg_a_irq_msk; // register and bitmap access

#endif

//______________________________ end of register list _______________________________//
//                                                                                   //

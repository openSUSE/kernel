#if IS_ENABLED(CONFIG_CRYPTO_DEV_PADLOCK_AES)
	"aes-padlock",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ARTPEC6)
	"artpec-gcm-aes",
	"artpec-hmac-sha256",
	"artpec-sha1",
	"artpec-sha256",
	"artpec6-cbc-aes",
	"artpec6-ctr-aes",
	"artpec6-ecb-aes",
	"artpec6-xts-aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ASPEED)
	"aspeed-cbc-aes",
	"aspeed-cfb-aes",
	"aspeed-ctr-aes",
	"aspeed-ecb-aes",
	"aspeed-hmac-sha1",
	"aspeed-hmac-sha224",
	"aspeed-hmac-sha256",
	"aspeed-hmac-sha384",
	"aspeed-hmac-sha512",
	"aspeed-hmac-sha512_224",
	"aspeed-hmac-sha512_256",
	"aspeed-ofb-aes",
	"aspeed-rsa",
	"aspeed-sha1",
	"aspeed-sha224",
	"aspeed-sha256",
	"aspeed-sha384",
	"aspeed-sha512",
	"aspeed-sha512_224",
	"aspeed-sha512_256",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ATMEL_AES)
	"atmel-authenc-hmac-sha1-cbc-aes",
	"atmel-authenc-hmac-sha224-cbc-aes",
	"atmel-authenc-hmac-sha256-cbc-aes",
	"atmel-authenc-hmac-sha384-cbc-aes",
	"atmel-authenc-hmac-sha512-cbc-aes",
	"atmel-cbc-aes",
	"atmel-cfb-aes",
	"atmel-cfb16-aes",
	"atmel-cfb32-aes",
	"atmel-cfb64-aes",
	"atmel-cfb8-aes",
	"atmel-ctr-aes",
	"atmel-ecb-aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ATMEL_ECC)
	"atmel-ecdh",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ATMEL_AES)
	"atmel-gcm-aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ATMEL_SHA)
	"atmel-hmac-sha1",
	"atmel-hmac-sha224",
	"atmel-hmac-sha256",
	"atmel-hmac-sha384",
	"atmel-hmac-sha512",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ATMEL_AES)
	"atmel-ofb-aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ATMEL_SHA)
	"atmel-sha1",
	"atmel-sha224",
	"atmel-sha256",
	"atmel-sha384",
	"atmel-sha512",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ATMEL_AES)
	"atmel-xts-aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_IXP4XX)
	"authenc(hmac(sha1),cbc(aes))-ixp4xx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SA2UL)
	"authenc(hmac(sha1),cbc(aes))-sa2ul",
	"authenc(hmac(sha256),cbc(aes))-sa2ul",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"authenc-digest_null-cbc-aes-chcr",
	"authenc-digest_null-rfc3686-ctr-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"authenc-hmac-sha1-cbc-aes-caam",
	"authenc-hmac-sha1-cbc-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"authenc-hmac-sha1-cbc-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"authenc-hmac-sha1-cbc-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"authenc-hmac-sha1-cbc-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"authenc-hmac-sha1-cbc-aes-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"authenc-hmac-sha1-cbc-aes-talitos",
	"authenc-hmac-sha1-cbc-aes-talitos-hsna",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"authenc-hmac-sha1-ecb-cipher_null-caam",
	"authenc-hmac-sha1-rfc3686-ctr-aes-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"authenc-hmac-sha1-rfc3686-ctr-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"authenc-hmac-sha1-rfc3686-ctr-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"authenc-hmac-sha1-rfc3686-ctr-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"authenc-hmac-sha224-cbc-aes-caam",
	"authenc-hmac-sha224-cbc-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"authenc-hmac-sha224-cbc-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"authenc-hmac-sha224-cbc-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"authenc-hmac-sha224-cbc-aes-talitos",
	"authenc-hmac-sha224-cbc-aes-talitos-hsna",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"authenc-hmac-sha224-ecb-cipher_null-caam",
	"authenc-hmac-sha224-rfc3686-ctr-aes-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"authenc-hmac-sha224-rfc3686-ctr-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"authenc-hmac-sha224-rfc3686-ctr-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"authenc-hmac-sha256-cbc-aes-caam",
	"authenc-hmac-sha256-cbc-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"authenc-hmac-sha256-cbc-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"authenc-hmac-sha256-cbc-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"authenc-hmac-sha256-cbc-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"authenc-hmac-sha256-cbc-aes-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_QCE_AEAD)
	"authenc-hmac-sha256-cbc-aes-qce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"authenc-hmac-sha256-cbc-aes-talitos",
	"authenc-hmac-sha256-cbc-aes-talitos-hsna",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"authenc-hmac-sha256-ecb-cipher_null-caam",
	"authenc-hmac-sha256-rfc3686-ctr-aes-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"authenc-hmac-sha256-rfc3686-ctr-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"authenc-hmac-sha256-rfc3686-ctr-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"authenc-hmac-sha256-rfc3686-ctr-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"authenc-hmac-sha384-cbc-aes-caam",
	"authenc-hmac-sha384-cbc-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"authenc-hmac-sha384-cbc-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"authenc-hmac-sha384-cbc-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"authenc-hmac-sha384-cbc-aes-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"authenc-hmac-sha384-ecb-cipher_null-caam",
	"authenc-hmac-sha384-rfc3686-ctr-aes-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"authenc-hmac-sha384-rfc3686-ctr-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"authenc-hmac-sha384-rfc3686-ctr-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"authenc-hmac-sha512-cbc-aes-caam",
	"authenc-hmac-sha512-cbc-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"authenc-hmac-sha512-cbc-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"authenc-hmac-sha512-cbc-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"authenc-hmac-sha512-cbc-aes-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"authenc-hmac-sha512-ecb-cipher_null-caam",
	"authenc-hmac-sha512-rfc3686-ctr-aes-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"authenc-hmac-sha512-rfc3686-ctr-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"authenc-hmac-sha512-rfc3686-ctr-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"authenc-xcbc-aes-cbc-aes-ccree",
	"authenc-xcbc-aes-rfc3686-ctr-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CAVIUM_CPT)
	"cavium-cbc-aes",
	"cavium-cfb-aes",
	"cavium-ecb-aes",
	"cavium-xts-aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_IXP4XX)
	"cbc(aes)-ixp4xx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"cbc-aes-caam",
	"cbc-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"cbc-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"cbc-aes-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"cbc-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"cbc-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_MXS_DCP)
	"cbc-aes-dcp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_GEODE)
	"cbc-aes-geode",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_AMLOGIC_GXL)
	"cbc-aes-gxl",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"cbc-aes-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4)
	"cbc-aes-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NIAGARA2)
	"cbc-aes-n2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NX_ENCRYPT)
	"cbc-aes-nx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OMAP_AES)
	"cbc-aes-omap",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_PADLOCK_AES)
	"cbc-aes-padlock",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_PPC4XX)
	"cbc-aes-ppc4xx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_QCE_SKCIPHER)
	"cbc-aes-qce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ROCKCHIP)
	"cbc-aes-rk",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_S5P)
	"cbc-aes-s5p",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SA2UL)
	"cbc-aes-sa2ul",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN4I_SS)
	"cbc-aes-sun4i-ss",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE)
	"cbc-aes-sun8i-ce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_SS)
	"cbc-aes-sun8i-ss",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"cbc-aes-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"cbc-paes-ccree",
	"ccm-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"ccm-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"ccm-aes-esp-iproc",
	"ccm-aes-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4)
	"ccm-aes-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NX_ENCRYPT)
	"ccm-aes-nx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_PPC4XX)
	"ccm-aes-ppc4xx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_QCE_AEAD)
	"ccm-aes-qce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"cfb-aes-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_PPC4XX)
	"cfb-aes-ppc4xx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"cmac-aes-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"cmac-aes-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"cmac-aes-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT)
	"cpt_cbc_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX_CPT)
	"cpt_cbc_aes",
	"cpt_cfb_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT)
	"cpt_ecb_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX_CPT)
	"cpt_ecb_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT)
	"cpt_hmac_sha1_cbc_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX_CPT)
	"cpt_hmac_sha1_cbc_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT)
	"cpt_hmac_sha1_ecb_null",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX_CPT)
	"cpt_hmac_sha1_ecb_null",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT)
	"cpt_hmac_sha256_cbc_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX_CPT)
	"cpt_hmac_sha256_cbc_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT)
	"cpt_hmac_sha256_ecb_null",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX_CPT)
	"cpt_hmac_sha256_ecb_null",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT)
	"cpt_hmac_sha384_cbc_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX_CPT)
	"cpt_hmac_sha384_cbc_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT)
	"cpt_hmac_sha384_ecb_null",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX_CPT)
	"cpt_hmac_sha384_ecb_null",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT)
	"cpt_hmac_sha512_cbc_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX_CPT)
	"cpt_hmac_sha512_cbc_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT)
	"cpt_hmac_sha512_ecb_null",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX_CPT)
	"cpt_hmac_sha512_ecb_null",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT)
	"cpt_rfc4106_gcm_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX_CPT)
	"cpt_rfc4106_gcm_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT)
	"cpt_xts_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OCTEONTX_CPT)
	"cpt_xts_aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_PPC4XX)
	"crypto4xx_rng",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_IXP4XX)
	"ctr(aes)-ixp4xx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"ctr-aes-caam",
	"ctr-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"ctr-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"ctr-aes-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"ctr-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"ctr-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"ctr-aes-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4)
	"ctr-aes-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NIAGARA2)
	"ctr-aes-n2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OMAP_AES)
	"ctr-aes-omap",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_PPC4XX)
	"ctr-aes-ppc4xx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_QCE_SKCIPHER)
	"ctr-aes-qce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_S5P)
	"ctr-aes-s5p",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"ctr-aes-talitos",
	"ctr-aes-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"ctr-paes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4)
	"cts-aes-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"cts-cbc-aes-ccree",
	"cts-cbc-paes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_IXP4XX)
	"ecb(aes)-ixp4xx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"ecb-aes-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"ecb-aes-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"ecb-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_MXS_DCP)
	"ecb-aes-dcp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_GEODE)
	"ecb-aes-geode",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_AMLOGIC_GXL)
	"ecb-aes-gxl",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"ecb-aes-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4)
	"ecb-aes-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NIAGARA2)
	"ecb-aes-n2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NX_ENCRYPT)
	"ecb-aes-nx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OMAP_AES)
	"ecb-aes-omap",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_PADLOCK_AES)
	"ecb-aes-padlock",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_PPC4XX)
	"ecb-aes-ppc4xx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_QCE_SKCIPHER)
	"ecb-aes-qce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ROCKCHIP)
	"ecb-aes-rk",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_S5P)
	"ecb-aes-s5p",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SA2UL)
	"ecb-aes-sa2ul",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SL3516)
	"ecb-aes-sl3516",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN4I_SS)
	"ecb-aes-sun4i-ss",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE)
	"ecb-aes-sun8i-ce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_SS)
	"ecb-aes-sun8i-ss",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"ecb-aes-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"ecb-paes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4)
	"ecdh-nist-p256-keembay-ocs",
	"ecdh-nist-p384-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"echainiv-authenc-hmac-sha1-cbc-aes-caam",
	"echainiv-authenc-hmac-sha1-cbc-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"echainiv-authenc-hmac-sha1-cbc-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"echainiv-authenc-hmac-sha224-cbc-aes-caam",
	"echainiv-authenc-hmac-sha224-cbc-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"echainiv-authenc-hmac-sha224-cbc-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"echainiv-authenc-hmac-sha256-cbc-aes-caam",
	"echainiv-authenc-hmac-sha256-cbc-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"echainiv-authenc-hmac-sha256-cbc-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"echainiv-authenc-hmac-sha384-cbc-aes-caam",
	"echainiv-authenc-hmac-sha384-cbc-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"echainiv-authenc-hmac-sha384-cbc-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"echainiv-authenc-hmac-sha512-cbc-aes-caam",
	"echainiv-authenc-hmac-sha512-cbc-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"echainiv-authenc-hmac-sha512-cbc-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"essiv-aes-ccree",
	"essiv-paes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_S5P)
	"exynos-sha1",
	"exynos-sha256",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EXYNOS_RNG)
	"exynos_rng",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"gcm-aes-caam",
	"gcm-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"gcm-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"gcm-aes-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"gcm-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"gcm-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"gcm-aes-esp-iproc",
	"gcm-aes-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_AES_SM4)
	"gcm-aes-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NX_ENCRYPT)
	"gcm-aes-nx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OMAP_AES)
	"gcm-aes-omap",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_PPC4XX)
	"gcm-aes-ppc4xx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_GEODE)
	"geode-aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"gmac-aes-esp-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_HISI_SEC)
	"hisi_sec_aes_cbc",
	"hisi_sec_aes_ctr",
	"hisi_sec_aes_ecb",
	"hisi_sec_aes_xts",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_HISI_TRNG)
	"hisi_stdrng",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"hmac-sha1-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"hmac-sha1-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"hmac-sha1-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"hmac-sha1-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"hmac-sha1-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"hmac-sha1-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NIAGARA2)
	"hmac-sha1-n2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_QCE_SHA)
	"hmac-sha1-qce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_SS)
	"hmac-sha1-sun8i-ss",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"hmac-sha1-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"hmac-sha224-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"hmac-sha224-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"hmac-sha224-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"hmac-sha224-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"hmac-sha224-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"hmac-sha224-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_HCU)
	"hmac-sha224-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NIAGARA2)
	"hmac-sha224-n2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"hmac-sha224-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"hmac-sha256-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"hmac-sha256-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"hmac-sha256-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"hmac-sha256-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"hmac-sha256-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"hmac-sha256-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_HCU)
	"hmac-sha256-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NIAGARA2)
	"hmac-sha256-n2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_QCE_SHA)
	"hmac-sha256-qce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"hmac-sha256-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"hmac-sha3-224-iproc",
	"hmac-sha3-256-iproc",
	"hmac-sha3-384-iproc",
	"hmac-sha3-512-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"hmac-sha384-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"hmac-sha384-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"hmac-sha384-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"hmac-sha384-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"hmac-sha384-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"hmac-sha384-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_HCU)
	"hmac-sha384-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"hmac-sha384-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"hmac-sha512-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"hmac-sha512-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"hmac-sha512-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"hmac-sha512-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"hmac-sha512-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"hmac-sha512-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_HCU)
	"hmac-sha512-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"hmac-sha512-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_HISI_HPRE)
	"hpre-dh",
	"hpre-ecdh-nist-p192",
	"hpre-ecdh-nist-p256",
	"hpre-ecdh-nist-p384",
	"hpre-rsa",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_IMGTEC_HASH)
	"img-sha1",
	"img-sha224",
	"img-sha256",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_MARVELL_CESA)
	"mv-cbc-aes",
	"mv-ecb-aes",
	"mv-hmac-sha1",
	"mv-hmac-sha256",
	"mv-sha1",
	"mv-sha256",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NITROX_CNN55XX)
	"n5_aes_gcm",
	"n5_cbc(aes)",
	"n5_cfb(aes)",
	"n5_cts(cbc(aes))",
	"n5_ecb(aes)",
	"n5_rfc3686(ctr(aes))",
	"n5_rfc4106",
	"n5_xts(aes)",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"ofb-aes-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"ofb-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"ofb-aes-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_PPC4XX)
	"ofb-aes-ppc4xx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"ofb-paes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OMAP_SHAM)
	"omap-hmac-sha1",
	"omap-hmac-sha224",
	"omap-hmac-sha256",
	"omap-hmac-sha384",
	"omap-hmac-sha512",
	"omap-sha1",
	"omap-sha224",
	"omap-sha256",
	"omap-sha384",
	"omap-sha512",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_VMX_ENCRYPT)
	"p8_aes",
	"p8_aes_cbc",
	"p8_aes_ctr",
	"p8_aes_xts",
	"p8_ghash",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"prng-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_QAT)
	"qat-dh",
	"qat-rsa",
	"qat_aes_cbc",
	"qat_aes_cbc_hmac_sha1",
	"qat_aes_cbc_hmac_sha256",
	"qat_aes_cbc_hmac_sha512",
	"qat_aes_ctr",
	"qat_aes_xts",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_QCOM_RNG)
	"qcom-rng",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_IXP4XX)
	"rfc3686(ctr(aes))-ixp4xx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"rfc3686-ctr-aes-caam",
	"rfc3686-ctr-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"rfc3686-ctr-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"rfc3686-ctr-aes-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"rfc3686-ctr-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NX_ENCRYPT)
	"rfc3686-ctr-aes-nx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_PPC4XX)
	"rfc3686-ctr-aes-ppc4xx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"rfc4106-gcm-aes-caam",
	"rfc4106-gcm-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"rfc4106-gcm-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"rfc4106-gcm-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"rfc4106-gcm-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NX_ENCRYPT)
	"rfc4106-gcm-aes-nx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_OMAP_AES)
	"rfc4106-gcm-aes-omap",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"rfc4309-ccm-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"rfc4309-ccm-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NX_ENCRYPT)
	"rfc4309-ccm-aes-nx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_QCE_AEAD)
	"rfc4309-ccm-aes-qce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"rfc4543-gcm-aes-caam",
	"rfc4543-gcm-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"rfc4543-gcm-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"rfc4543-gcm-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ROCKCHIP)
	"rk-sha1",
	"rk-sha256",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"rsa-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"rsa-ccp",
	"rsa-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SAFEXCEL)
	"safexcel-authenc-hmac-sha1-cbc-aes",
	"safexcel-authenc-hmac-sha1-ctr-aes",
	"safexcel-authenc-hmac-sha224-cbc-aes",
	"safexcel-authenc-hmac-sha224-ctr-aes",
	"safexcel-authenc-hmac-sha256-cbc-aes",
	"safexcel-authenc-hmac-sha256-ctr-aes",
	"safexcel-authenc-hmac-sha384-cbc-aes",
	"safexcel-authenc-hmac-sha384-ctr-aes",
	"safexcel-authenc-hmac-sha512-cbc-aes",
	"safexcel-authenc-hmac-sha512-ctr-aes",
	"safexcel-cbc-aes",
	"safexcel-cbcmac-aes",
	"safexcel-ccm-aes",
	"safexcel-cfb-aes",
	"safexcel-cmac-aes",
	"safexcel-ctr-aes",
	"safexcel-ecb-aes",
	"safexcel-gcm-aes",
	"safexcel-hmac-sha1",
	"safexcel-hmac-sha224",
	"safexcel-hmac-sha256",
	"safexcel-hmac-sha3-224",
	"safexcel-hmac-sha3-256",
	"safexcel-hmac-sha3-384",
	"safexcel-hmac-sha3-512",
	"safexcel-hmac-sha384",
	"safexcel-hmac-sha512",
	"safexcel-ofb-aes",
	"safexcel-rfc4106-gcm-aes",
	"safexcel-rfc4309-ccm-aes",
	"safexcel-rfc4543-gcm-aes",
	"safexcel-sha1",
	"safexcel-sha224",
	"safexcel-sha256",
	"safexcel-sha3-224",
	"safexcel-sha3-256",
	"safexcel-sha3-384",
	"safexcel-sha3-512",
	"safexcel-sha384",
	"safexcel-sha512",
	"safexcel-xcbc-aes",
	"safexcel-xts-aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SAHARA)
	"sahara-cbc-aes",
	"sahara-ecb-aes",
	"sahara-sha1",
	"sahara-sha256",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"seqiv-authenc-hmac-sha1-rfc3686-ctr-aes-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"seqiv-authenc-hmac-sha1-rfc3686-ctr-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"seqiv-authenc-hmac-sha224-rfc3686-ctr-aes-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"seqiv-authenc-hmac-sha224-rfc3686-ctr-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"seqiv-authenc-hmac-sha256-rfc3686-ctr-aes-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"seqiv-authenc-hmac-sha256-rfc3686-ctr-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"seqiv-authenc-hmac-sha384-rfc3686-ctr-aes-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"seqiv-authenc-hmac-sha384-rfc3686-ctr-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"seqiv-authenc-hmac-sha512-rfc3686-ctr-aes-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"seqiv-authenc-hmac-sha512-rfc3686-ctr-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"sha1-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"sha1-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"sha1-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"sha1-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"sha1-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_MXS_DCP)
	"sha1-dcp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"sha1-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NIAGARA2)
	"sha1-n2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_PADLOCK_SHA)
	"sha1-padlock",
	"sha1-padlock-nano",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_QCE_SHA)
	"sha1-qce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SA2UL)
	"sha1-sa2ul",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN4I_SS)
	"sha1-sun4i-ss",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE)
	"sha1-sun8i-ce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_SS)
	"sha1-sun8i-ss",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"sha1-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"sha224-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"sha224-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"sha224-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"sha224-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"sha224-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"sha224-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_HCU)
	"sha224-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NIAGARA2)
	"sha224-n2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE)
	"sha224-sun8i-ce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_SS)
	"sha224-sun8i-ss",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"sha224-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"sha256-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"sha256-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"sha256-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"sha256-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"sha256-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_MXS_DCP)
	"sha256-dcp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"sha256-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_HCU)
	"sha256-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NIAGARA2)
	"sha256-n2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NX_ENCRYPT)
	"sha256-nx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_PADLOCK_SHA)
	"sha256-padlock",
	"sha256-padlock-nano",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_QCE_SHA)
	"sha256-qce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SA2UL)
	"sha256-sa2ul",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE)
	"sha256-sun8i-ce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_SS)
	"sha256-sun8i-ss",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"sha256-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"sha3-224-iproc",
	"sha3-256-iproc",
	"sha3-384-iproc",
	"sha3-512-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"sha384-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"sha384-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"sha384-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"sha384-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"sha384-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"sha384-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_HCU)
	"sha384-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE)
	"sha384-sun8i-ce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"sha384-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"sha512-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"sha512-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"sha512-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"sha512-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"sha512-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"sha512-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_KEEMBAY_OCS_HCU)
	"sha512-keembay-ocs",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_NX_ENCRYPT)
	"sha512-nx",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SA2UL)
	"sha512-sa2ul",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE)
	"sha512-sun8i-ce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_TALITOS)
	"sha512-talitos",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_STM32_CRYP)
	"stm32-cbc-aes",
	"stm32-ccm-aes",
	"stm32-ctr-aes",
	"stm32-ecb-aes",
	"stm32-gcm-aes",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_STM32_HASH)
	"stm32-hmac-sha1",
	"stm32-hmac-sha224",
	"stm32-hmac-sha256",
	"stm32-sha1",
	"stm32-sha224",
	"stm32-sha256",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN4I_SS)
	"sun4i_ss_rng",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_CE)
	"sun8i-ce-prng",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_SUN8I_SS)
	"sun8i-ss-prng",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_VIRTIO)
	"virtio-crypto-rsa",
	"virtio-pkcs1-rsa-with-sha1",
	"virtio_crypto_aes_cbc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"xcbc-aes-caam",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"xcbc-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"xcbc-aes-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ZYNQMP_AES)
	"xilinx-zynqmp-aes-gcm",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_CAAM_JR)
	"xts-aes-caam",
	"xts-aes-caam-qi",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_FSL_DPAA2_CAAM)
	"xts-aes-caam-qi2",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCP_CRYPTO)
	"xts-aes-ccp",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"xts-aes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CHELSIO)
	"xts-aes-chcr",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_BCM_SPU)
	"xts-aes-iproc",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_QCE_SKCIPHER)
	"xts-aes-qce",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_CCREE)
	"xts-paes-ccree",
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_ZYNQMP_SHA3)
	"zynqmp-sha3-384",
#endif

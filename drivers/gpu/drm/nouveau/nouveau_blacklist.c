/*
 * List of PCI SSIDs to be disabled as default (Turing and Ampere models)
 * that are supported by Nvidia opengpu driver
 */

static const u16 nouveau_probe_blacklist[] = {
	0x1E02, /* NVIDIA TITAN RTX */
	0x1E04, /* NVIDIA GeForce RTX 2080 Ti */
	0x1E07, /* NVIDIA GeForce RTX 2080 Ti */
	0x1E30, /* Quadro RTX 8000 */
	0x1E36, /* Quadro RTX 6000 */
	0x1E78, /* Quadro RTX 6000 */
	0x1E81, /* NVIDIA GeForce RTX 2080 SUPER */
	0x1E82, /* NVIDIA GeForce RTX 2080 */
	0x1E84, /* NVIDIA GeForce RTX 2070 SUPER */
	0x1E87, /* NVIDIA GeForce RTX 2080 */
	0x1E89, /* NVIDIA GeForce RTX 2060 */
	0x1E90, /* NVIDIA GeForce RTX 2080 with Max-Q Design */
	0x1E91, /* NVIDIA GeForce RTX 2070 Super with Max-Q Design */
	0x1E93, /* NVIDIA GeForce RTX 2080 Super with Max-Q Design */
	0x1EB0, /* Quadro RTX 5000 */
	0x1EB1, /* Quadro RTX 4000 */
	0x1EB5, /* Quadro RTX 5000 with Max-Q Design */
	0x1EB6, /* Quadro RTX 4000 with Max-Q Design */
	0x1EC2, /* NVIDIA GeForce RTX 2070 SUPER */
	0x1EC7, /* NVIDIA GeForce RTX 2070 SUPER */
	0x1ED0, /* NVIDIA GeForce RTX 2080 with Max-Q Design */
	0x1ED1, /* NVIDIA GeForce RTX 2070 Super with Max-Q Design */
	0x1ED3, /* NVIDIA GeForce RTX 2080 Super with Max-Q Design */
	0x1EF5, /* Quadro RTX 5000 */
	0x1F02, /* NVIDIA GeForce RTX 2070 */
	0x1F03, /* NVIDIA GeForce RTX 2060 */
	0x1F06, /* NVIDIA GeForce RTX 2060 SUPER */
	0x1F07, /* NVIDIA GeForce RTX 2070 */
	0x1F08, /* NVIDIA GeForce RTX 2060 */
	0x1F0A, /* NVIDIA GeForce GTX 1650 */
	0x1F10, /* NVIDIA GeForce RTX 2070 with Max-Q Design */
	0x1F11, /* NVIDIA GeForce RTX 2060 */
	0x1F12, /* NVIDIA GeForce RTX 2060 with Max-Q Design */
	0x1F14, /* NVIDIA GeForce RTX 2070 with Max-Q Design */
	0x1F15, /* NVIDIA GeForce RTX 2060 */
	0x1F36, /* Quadro RTX 3000 with Max-Q Design */
	0x1F42, /* NVIDIA GeForce RTX 2060 SUPER */
	0x1F47, /* NVIDIA GeForce RTX 2060 SUPER */
	0x1F50, /* NVIDIA GeForce RTX 2070 with Max-Q Design */
	0x1F51, /* NVIDIA GeForce RTX 2060 */
	0x1F54, /* NVIDIA GeForce RTX 2070 with Max-Q Design */
	0x1F55, /* NVIDIA GeForce RTX 2060 */
	0x1F76, /* Matrox D-Series D2480 */
	0x1F82, /* NVIDIA GeForce GTX 1650 */
	0x1F83, /* NVIDIA GeForce GTX 1630 */
	0x1F91, /* NVIDIA GeForce GTX 1650 with Max-Q Design */
	0x1F95, /* NVIDIA GeForce GTX 1650 Ti with Max-Q Design */
	0x1F96, /* NVIDIA GeForce GTX 1650 with Max-Q Design */
	0x1F97, /* NVIDIA GeForce MX450 */
	0x1F98, /* NVIDIA GeForce MX450 */
	0x1F99, /* NVIDIA GeForce GTX 1650 with Max-Q Design */
	0x1F9C, /* NVIDIA GeForce MX450 */
	0x1F9D, /* NVIDIA GeForce GTX 1650 with Max-Q Design */
	0x1F9F, /* NVIDIA GeForce MX550 */
	0x1FA0, /* NVIDIA GeForce MX550 */
	0x1FB0, /* NVIDIA T1000 */
	0x1FB1, /* NVIDIA T600 */
	0x1FB2, /* NVIDIA T400 */
	0x1FB6, /* NVIDIA T600 Laptop GPU */
	0x1FB7, /* NVIDIA T550 Laptop GPU */
	0x1FB8, /* Quadro T2000 with Max-Q Design */
	0x1FB9, /* Quadro T1000 with Max-Q Design */
	0x1FBA, /* NVIDIA T600 Laptop GPU */
	0x1FBB, /* NVIDIA T500 */
	0x1FBC, /* NVIDIA T1200 Laptop GPU */
	0x1FDD, /* NVIDIA GeForce GTX 1650 */
	0x1FF0, /* NVIDIA T1000 8GB */
	0x1FF2, /* NVIDIA T400 4GB */
	0x1FF9, /* Quadro T1000 */
	0x20F3, /* NVIDIA A800-SXM4-80GB */
	0x20F5, /* NVIDIA A800 80GB PCIe LC */
	0x2182, /* NVIDIA GeForce GTX 1660 Ti */
	0x2184, /* NVIDIA GeForce GTX 1660 */
	0x2187, /* NVIDIA GeForce GTX 1650 SUPER */
	0x2188, /* NVIDIA GeForce GTX 1650 */
	0x2191, /* NVIDIA GeForce GTX 1660 Ti with Max-Q Design */
	0x2192, /* NVIDIA GeForce GTX 1650 Ti */
	0x21C4, /* NVIDIA GeForce GTX 1660 SUPER */
	0x21D1, /* NVIDIA GeForce GTX 1660 Ti */
	0x2203, /* NVIDIA GeForce RTX 3090 Ti */
	0x2204, /* NVIDIA GeForce RTX 3090 */
	0x2206, /* NVIDIA GeForce RTX 3080 */
	0x2207, /* NVIDIA GeForce RTX 3070 Ti */
	0x2208, /* NVIDIA GeForce RTX 3080 Ti */
	0x220A, /* NVIDIA GeForce RTX 3080 */
	0x220D, /* NVIDIA CMP 90HX */
	0x2216, /* NVIDIA GeForce RTX 3080 */
	0x2230, /* NVIDIA RTX A6000 */
	0x2231, /* NVIDIA RTX A5000 */
	0x2232, /* NVIDIA RTX A4500 */
	0x2233, /* NVIDIA RTX A5500 */
	0x2238, /* NVIDIA A10M */
	0x2322, /* NVIDIA H800 PCIe */
	0x2324, /* NVIDIA H800 */
	0x2330, /* NVIDIA H100 80GB HBM3 */
	0x2331, /* NVIDIA H100 PCIe */
	0x2339, /* NVIDIA H100 */
	0x2414, /* NVIDIA GeForce RTX 3060 Ti */
	0x2420, /* NVIDIA GeForce RTX 3080 Ti Laptop GPU */
	0x2438, /* NVIDIA RTX A5500 Laptop GPU */
	0x2460, /* NVIDIA GeForce RTX 3080 Ti Laptop GPU */
	0x2482, /* NVIDIA GeForce RTX 3070 Ti */
	0x2484, /* NVIDIA GeForce RTX 3070 */
	0x2486, /* NVIDIA GeForce RTX 3060 Ti */
	0x2487, /* NVIDIA GeForce RTX 3060 */
	0x2488, /* NVIDIA GeForce RTX 3070 */
	0x2489, /* NVIDIA GeForce RTX 3060 Ti */
	0x248A, /* NVIDIA CMP 70HX */
	0x249C, /* NVIDIA GeForce RTX 3060 Laptop GPU */
	0x249D, /* NVIDIA GeForce RTX 3070 Laptop GPU */
	0x24A0, /* NVIDIA GeForce RTX 3060 Laptop GPU */
	0x24B0, /* NVIDIA RTX A4000 */
	0x24B1, /* NVIDIA RTX A4000H */
	0x24B6, /* NVIDIA RTX A5000 Laptop GPU */
	0x24B7, /* NVIDIA RTX A4000 Laptop GPU */
	0x24B8, /* NVIDIA RTX A3000 Laptop GPU */
	0x24B9, /* NVIDIA RTX A3000 12GB Laptop GPU */
	0x24BA, /* NVIDIA RTX A4500 Laptop GPU */
	0x24BB, /* NVIDIA RTX A3000 12GB Laptop GPU */
	0x24C9, /* NVIDIA GeForce RTX 3060 Ti */
	0x24DC, /* NVIDIA GeForce RTX 3080 Laptop GPU */
	0x24DD, /* NVIDIA GeForce RTX 3070 Laptop GPU */
	0x24E0, /* NVIDIA GeForce RTX 3070 Ti Laptop GPU */
	0x24FA, /* NVIDIA RTX A4500 Embedded GPU */
	0x2503, /* NVIDIA GeForce RTX 3060 */
	0x2504, /* NVIDIA GeForce RTX 3060 */
	0x2507, /* NVIDIA GeForce RTX 3050 */
	0x2508, /* NVIDIA GeForce RTX 3050 OEM */
	0x2520, /* NVIDIA GeForce RTX 3060 Laptop GPU */
	0x2521, /* NVIDIA GeForce RTX 3060 Laptop GPU */
	0x2523, /* NVIDIA GeForce RTX 3050 Ti Laptop GPU */
	0x2531, /* NVIDIA RTX A2000 */
	0x2544, /* NVIDIA GeForce RTX 3060 */
	0x2560, /* NVIDIA GeForce RTX 3060 Laptop GPU */
	0x2563, /* NVIDIA GeForce RTX 3050 Ti Laptop GPU */
	0x2571, /* NVIDIA RTX A2000 12GB */
	0x2582, /* NVIDIA GeForce RTX 3050 */
	0x25A0, /* NVIDIA GeForce RTX 3060 Laptop GPU */
	0x25A2, /* NVIDIA GeForce RTX 3060 Laptop GPU */
	0x25A5, /* NVIDIA GeForce RTX 3050 Laptop GPU */
	0x25A6, /* NVIDIA GeForce MX570 */
	0x25A7, /* NVIDIA GeForce RTX 2050 */
	0x25A9, /* NVIDIA GeForce RTX 2050 */
	0x25AA, /* NVIDIA GeForce MX570 A */
	0x25AB, /* NVIDIA GeForce RTX 3050 4GB Laptop GPU */
	0x25AC, /* NVIDIA GeForce RTX 3050 6GB Laptop GPU */
	0x25AD, /* NVIDIA GeForce RTX 2050 */
	0x25B8, /* NVIDIA RTX A2000 Laptop GPU */
	0x25B9, /* NVIDIA RTX A1000 Laptop GPU */
	0x25BA, /* NVIDIA RTX A2000 8GB Laptop GPU */
	0x25BB, /* NVIDIA RTX A500 Laptop GPU */
	0x25E0, /* NVIDIA GeForce RTX 3050 Ti Laptop GPU */
	0x25E2, /* NVIDIA GeForce RTX 3050 Laptop GPU */
	0x25E5, /* NVIDIA GeForce RTX 3050 Laptop GPU */
	0x25EC, /* NVIDIA GeForce RTX 3050 6GB Laptop GPU */
	0x25ED, /* NVIDIA GeForce RTX 2050 */
	0x25F9, /* NVIDIA RTX A1000 Embedded GPU */
	0x25FA, /* NVIDIA RTX A2000 Embedded GPU */
	0x25FB, /* NVIDIA RTX A500 Embedded GPU */
	0x2684, /* NVIDIA GeForce RTX 4090 */
	0x26B1, /* NVIDIA RTX 6000 Ada Generation */
	0x26B5, /* NVIDIA L40 */
	0x2704, /* NVIDIA GeForce RTX 4080 */
	0x2717, /* NVIDIA GeForce RTX 4090 Laptop GPU */
	0x2757, /* NVIDIA GeForce RTX 4090 Laptop GPU */
	0x2782, /* NVIDIA GeForce RTX 4070 Ti */
	0x27A0, /* NVIDIA GeForce RTX 4080 Laptop GPU */
	0x27B0, /* NVIDIA RTX 4000 SFF Ada Generation */
	0x27B8, /* NVIDIA L4 */
	0x27E0, /* NVIDIA GeForce RTX 4080 Laptop GPU */
	0x2820, /* NVIDIA GeForce RTX 4070 Laptop GPU */
	0x2860, /* NVIDIA GeForce RTX 4070 Laptop GPU */
	0x28A0, /* NVIDIA GeForce RTX 4060 Laptop GPU */
	0x28A1, /* NVIDIA GeForce RTX 4050 Laptop GPU */
	0x28E0, /* NVIDIA GeForce RTX 4060 Laptop GPU */
	0x28E1, /* NVIDIA GeForce RTX 4050 Laptop GPU */

	0 /* terminator */
};


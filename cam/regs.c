/*
 * regs.c -- read a camera block's registers through /dev/mem.
 *
 * The one measurement that was missing: the state of the camera's
 * hardware *while it works*.  Under the ROM's kernel the camera takes
 * a photograph; under ours the frame comes out empty.  The driver
 * prints the CSID's registers when the camera is released, but only
 * our own kernel has those prints -- so the ROM's kernel's registers
 * have to be read from the outside, and the only way to do that is
 * through /dev/mem, which needs a root shell.
 *
 * Usage:
 *   regs <base> [offset ...]
 *
 * With no offsets, the interesting ones of the CSID are read.  The
 * base addresses come from the device tree:
 *
 *   qcom,csid@fda08000   CSID0     0xfda08000
 *   qcom,csid@fda08400   CSID1     0xfda08400
 *   qcom,csid@fda08800   CSID2     0xfda08800
 *   qcom,csid@fda08c00   CSID3     0xfda08c00
 *   qcom,ispif@fda0a000  ISP-IF    0xfda0a000
 *   qcom,vfe@1a20000     VFE       0x01a20000
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct reg {
	unsigned off;
	const char *name;
};

/* the CSID's register map (csi3.0) */
static const struct reg {
	unsigned off;
	const char *name;
} csid_regs[] = {
	{0x00, "HW_VERSION"},
	{0x04, "CORE_CTRL_0"},
	{0x08, "CORE_CTRL_1"},
	{0x0c, "RST_CMD"},
	{0x10, "LUT_VC_0"},
	{0x14, "LUT_VC_1"},
	{0x18, "LUT_VC_2"},
	{0x1c, "LUT_VC_3"},
	{0x20, "CID_0_CFG"},
	{0x24, "CID_1_CFG"},
	{0x28, "CID_2_CFG"},
	{0x2c, "CID_3_CFG"},
	{0x60, "IRQ_MASK"},
	{0x64, "IRQ_STATUS"},
	{0x68, "IRQ_CLEAR"},
	{0x6c, "CAPTURED_UNMAPPED_LONG"},
	{0x70, "CAPTURED_MAPPED_LONG"},
	{0x74, "CAPTURED_SHORT"},
	{0x78, "CAPTURED_LONG_HDR"},
	{0x7c, "CAPTURED_LONG_FTR"},
	{0x90, "STATS_TOTAL_PKTS_RCVD"},
	{0x94, "STATS_ECC"},
	{0x98, "STATS_CRC"},
	{0xa0, "TG_CTRL"},
	{0xa4, "TG_VC_CFG"},
	{0xac, "TG_DT_0_CFG"},
	{0xb0, "TG_DT_1_CFG"},
	{0xb4, "TG_DT_2_CFG"},
};

/* the ISP-IF's register map */
static const struct reg {
	unsigned off;
	const char *name;
} ispif_regs[] = {
	{0x0, "CTRL_0"},
	{0x8, "CTRL_2"},
	{0x10, "CTRL_3"},
	{0x18, "CTRL_4"},
	{0x20, "CTRL_5"},
	{0x28, "CTRL_6"},
	{0x30, "CTRL_7"},
	{0x38, "CTRL_8"},
	{0x40, "CTRL_9"},
	{0x48, "CTRL_10"},
	{0x50, "CTRL_11"},
	{0x58, "CTRL_12"},
	{0x60, "CTRL_13"},
	{0x68, "CTRL_14"},
	{0x70, "CTRL_15"},
	{0x78, "CTRL_16"},
	{0x80, "CTRL_17"},
	{0x88, "CTRL_18"},
	{0x90, "CTRL_19"},
	{0x98, "CTRL_20"},
	{0xa0, "CTRL_21"},
	{0xa8, "CTRL_22"},
	{0xb0, "CTRL_23"},
	{0xb8, "CTRL_24"},
	{0xc0, "CTRL_25"},
	{0xc8, "CTRL_26"},
	{0xd0, "CTRL_27"},
	{0xd8, "CTRL_28"},
	{0xe0, "CTRL_29"},
	{0xe8, "CTRL_30"},
	{0xf0, "CTRL_31"},
	{0xf8, "CTRL_32"},
	{0x100, "CTRL_33"},
	{0x108, "CTRL_34"},
	{0x110, "CTRL_35"},
	{0x118, "CTRL_36"},
	{0x120, "CTRL_37"},
	{0x128, "CTRL_38"},
	{0x130, "CTRL_39"},
	{0x138, "CTRL_40"},
	{0x140, "CTRL_41"},
	{0x148, "CTRL_42"},
	{0x150, "CTRL_43"},
	{0x158, "CTRL_44"},
	{0x160, "CTRL_45"},
	{0x168, "CTRL_46"},
	{0x170, "CTRL_47"},
	{0x178, "CTRL_48"},
	{0x180, "CTRL_49"},
	{0x188, "CTRL_50"},
	{0x190, "CTRL_51"},
	{0x198, "CTRL_52"},
	{0x1a0, "CTRL_53"},
	{0x1a8, "CTRL_54"},
	{0x1b0, "CTRL_55"},
	{0x1b8, "CTRL_56"},
	{0x1c0, "CTRL_57"},
	{0x1c8, "CTRL_58"},
	{0x1d0, "CTRL_59"},
	{0x1d8, "CTRL_60"},
	{0x1e0, "CTRL_61"},
	{0x1e8, "CTRL_62"},
	{0x1f0, "CTRL_63"},
	{0x1f8, "CTRL_64"},
	{0x200, "CTRL_65"},
	{0x208, "CTRL_66"},
	{0x210, "CTRL_67"},
	{0x218, "CTRL_68"},
	{0x220, "CTRL_69"},
	{0x228, "CTRL_70"},
	{0x230, "CTRL_71"},
	{0x238, "CTRL_72"},
	{0x240, "CTRL_73"},
	{0x248, "CTRL_74"},
	{0x250, "CTRL_75"},
	{0x258, "CTRL_76"},
	{0x260, "CTRL_77"},
	{0x268, "CTRL_78"},
	{0x270, "CTRL_79"},
	{0x278, "CTRL_80"},
	{0x280, "CTRL_81"},
	{0x288, "CTRL_82"},
	{0x290, "CTRL_83"},
	{0x298, "CTRL_84"},
	{0x2a0, "CTRL_85"},
	{0x2a8, "CTRL_86"},
	{0x2b0, "CTRL_87"},
	{0x2b8, "CTRL_88"},
	{0x2c0, "CTRL_89"},
	{0x2c8, "CTRL_90"},
	{0x2d0, "CTRL_91"},
	{0x2d8, "CTRL_92"},
	{0x2e0, "CTRL_93"},
	{0x2e8, "CTRL_94"},
	{0x2f0, "CTRL_95"},
	{0x2f8, "CTRL_96"},
	{0x300, "CTRL_97"},
	{0x308, "CTRL_98"},
	{0x310, "CTRL_99"},
	{0x318, "CTRL_100"},
};

/* the VFE40's camera front end (the CAMIF) */
static const struct reg {
	unsigned off;
	const char *name;
} vfe_regs[] = {
	{0x1e0, "CAMIF_CTRL"},
	{0x1e4, "CAMIF_CTRL2"},
	{0x1ec, "CAMIF_FRAME_SIZE"},
	{0x1f0, "CAMIF_WINDOW_X"},
	{0x1f4, "CAMIF_WINDOW_Y"},
	{0x2e8, "CAMIF_VIOLATION_STATUS"},
	{0x2f4, "CAMIF_MUX"},
	{0x300, "CAMIF_FRAME_SIZE2"},
	{0x304, "CAMIF_LAST_PIXEL"},
	{0x308, "CAMIF_LAST_LINE"},
	{0x31c, "CAMIF_ERR_STATUS"},
	{0x48, "VFE_ERR_STATUS1"},
	{0x6fc, "CAMIF_CFG"},
};

int main(int argc, char **argv)
{
	unsigned long base = 0xfda08000UL;
	int fd, i, n = 0;
	unsigned offs[128];
	const char *names[128];
	const struct reg *table = csid_regs;
	int ntable = (int)(sizeof(csid_regs) / sizeof(csid_regs[0]));

	if (argc < 2) {
		fprintf(stderr,
			"usage: %s <base> [csid|ispif|vfe] [offset ...]\n",
			argv[0]);
		return 1;
	}
	base = strtoul(argv[1], NULL, 0);
	if (argc > 2 && strcmp(argv[2], "ispif") == 0) {
		table = ispif_regs;
		ntable = (int)(sizeof(ispif_regs) / sizeof(ispif_regs[0]));
	} else if (argc > 2 && strcmp(argv[2], "vfe") == 0) {
		table = vfe_regs;
		ntable = (int)(sizeof(vfe_regs) / sizeof(vfe_regs[0]));
	}
	if (argc > 2 && (strcmp(argv[2], "ispif") == 0 ||
			 strcmp(argv[2], "vfe") == 0))
		argv++;
	if (argc > 2) {
		for (i = 2; i < argc && n < 128; i++) {
			offs[n] = strtoul(argv[i], NULL, 0);
			names[n] = NULL;
			n++;
		}
	} else {
		for (i = 0; i < ntable; i++) {
			offs[n] = table[i].off;
			names[n] = table[i].name;
			n++;
		}
	}

	fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd < 0) {
		perror("/dev/mem");
		return 1;
	}
	printf("base 0x%08lx\n", base);
	for (i = 0; i < n; i++) {
		unsigned v = 0;
		if (pread(fd, &v, 4, (off_t)(base + offs[i])) != 4) {
			printf("  0x%03x  <unreadable>\n", offs[i]);
			continue;
		}
		if (names[i])
			printf("  0x%03x %-24s = 0x%08x\n", offs[i], names[i], v);
		else
			printf("  0x%03x %-24s = 0x%08x\n", offs[i], "", v);
	}
	close(fd);
	return 0;
}

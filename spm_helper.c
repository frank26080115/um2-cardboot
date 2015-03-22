#include "main.h"

#ifdef SCAN_FOR_SPM_SEQUENCE

addr_t spm_seq_addr = 0;

addr_t scan_for_spm(void)
{
	dbg_printf("searching for SPM sequence\r\n");

	addr_t i;
	uint8_t j;
	uint32_t buf[3];
	uint32_t match[3];
	match[0] = 0x00579380;
	match[1] = 0x27EE95E8;
	match[2] = 0x940927FF;
	for (i = (FLASHEND + 1 - 0x8000); i < FLASHEND; i++)
	{
		for (j = 0; j < 3; j++) {
			buf[j] = pgm_read_dword_at(i + (j * 4));
		}
		if (buf[0] == match[0] &&
			buf[1] == match[1] &&
			buf[2] == match[2]) {
			dbg_printf("SPM sequence found at 0x%04X%04X\r\n", ((uint16_t*)&i)[1], ((uint16_t*)&i)[0]);
			return i;
		}
	}
	dbg_printf("unable to find SPM sequence\r\n");
	return 0;
}

addr_t try_scan_for_spm(void)
{
	if (spm_seq_addr == 0) {
		spm_seq_addr = scan_for_spm();
	}
	return spm_seq_addr;
}

#endif
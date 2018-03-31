/*
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Tom Cubie <tangliang@allwinnertech.com>
 *
 * Boot an image which is generated by android mkbootimg tool
 *
 * (C) Copyright 2000-2003
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*
 * Misc boot support
 */
#include <common.h>
#include <command.h>
#include <net.h>
//#include <sunxi_board.h>
#include <android_image.h>
#include <fdtdec.h>
#include <asm/io.h>
#include <fdt_support.h>
#include <image.h>
#include <sunxi_board.h>
#include <power/sunxi/pmu.h>
#include <smc.h>


#ifndef CFG_ANDROID_IMAGE_PAGE_SIZE
	#define CFG_ANDROID_IMAGE_PAGE_SIZE 2048
#endif


DECLARE_GLOBAL_DATA_PTR;


#ifdef CONFIG_CMD_BOOTA
int __do_sunxi_boot_signature(cmd_tbl_t * cmdtp, int flag, int argc, char * const argv[])
{
	return 0;
}
int do_sunxi_boot_signature(cmd_tbl_t * cmdtp, int flag, int argc, char * const argv[])
	__attribute__((weak, alias("__do_sunxi_boot_signature")));



//static unsigned char boot_hdr[512];

typedef void (*Kernel_Entry)(int zero,int machine_id,void *fdt_addr);


void do_nonsec_virt_switch(void)
{
#if defined(CONFIG_ARMV7_NONSEC) || defined(CONFIG_ARMV7_VIRT)
	if (armv7_switch_nonsec() == 0)
#ifdef CONFIG_ARMV7_VIRT
		if (armv7_switch_hyp() == 0)
			debug("entered HYP mode\n");
#else
		debug("entered non-secure state\n");
#endif
#endif

#ifdef CONFIG_ARM64
	smp_kick_all_cpus();
	flush_dcache_all();	/* flush cache before swtiching to EL2 */
	armv8_switch_to_el2();
#ifdef CONFIG_ARMV8_SWITCH_TO_EL1
	armv8_switch_to_el1();
#endif
#endif
}


static void announce_and_cleanup(int fake)
{
	printf("prepare for kernel\n");
	axp_set_next_poweron_status(PMU_PRE_SYS_MODE);
#ifdef CONFIG_SUNXI_DISPLAY
	board_display_wait_lcd_open();		//add by jerry
	board_display_set_exit_mode(1);
#endif
	sunxi_board_close_source();

#ifdef CONFIG_BOOTSTAGE_FDT
	bootstage_fdt_add_report();
#endif
#ifdef CONFIG_BOOTSTAGE_REPORT
	bootstage_report();
#endif

#ifdef CONFIG_USB_DEVICE
	udc_disconnect();
#endif
	cleanup_before_linux();
	printf("\n-%s %d-Starting kernel ...%s\n\n",__FILE__,__LINE__, fake ?
		"(fake run for tracing)" : "");
}

/*
#ifndef CONFIG_ARM64

#define ENTRY_ADDR0_L    (SUNXI_CPUX_CFG_BASE+0xA0)
#define ENTRY_ADDR0_H    (SUNXI_CPUX_CFG_BASE+0xA4)

void uboot_jmpto_kernel(ulong addr,ulong dtb_base)
{
	//set jmp addr
	writel(addr,ENTRY_ADDR0_L);
	writel(0,ENTRY_ADDR0_H);


	//set dtb ,X0 save the addr of the dtb
	asm volatile("mov r0,%0\n": :"r"(dtb_base));


	//set cpu to AA64 execution state when the cpu boots into after a warm reset
	asm volatile("MRC p15,0,r1,c12,c0,2");
	asm volatile("ORR r1,r1,#(0x3<<0)");
	asm volatile("DSB");
	asm volatile("MCR p15,0,r1,c12,c0,2");
	asm volatile("ISB");
__LOOP:
	asm volatile("WFI");
	goto __LOOP;

}
#endif
*/
int do_boota_linux (struct andr_img_hdr *hdr,void * dtb_base)
{
	int fake = 0;
	Kernel_Entry kernel_entry = NULL;


	kernel_entry = (Kernel_Entry)(ulong)(hdr->kernel_addr);

	debug("## Transferring control to Linux (at address %lx)...\n",
		(ulong) kernel_entry);


	//while(*(uint*)(0x4a000000) != 9);
#ifdef CONFIG_ARM64
	do_nonsec_virt_switch();
	announce_and_cleanup(fake);
	kernel_entry(dtb_base);
#else
	announce_and_cleanup(fake);
	if(sunxi_probe_secure_monitor())
	{
		arm_svc_run_os((ulong)hdr->kernel_addr, (ulong)dtb_base,  0);
	}
	else
	{
		//uboot_jmpto_kernel((ulong)hdr->kernel_addr, (ulong)dtb_base);
                kernel_entry(0,0xffffffff,dtb_base);
	}
#endif
	return 0;
}

void * memcpy2(void * dest,const void * src,__kernel_size_t n)
{
	if (src == dest)
		return dest;
	if (src < dest) {
		if (src + n > dest) {
			memcpy((void*) (dest + (dest - src)), dest, src + n - dest);
			n = dest - src;
		}
		memcpy(dest, src, n);
	} else {
		memcpy(dest, src, n);
	}
	return dest;
}

void update_bootargs(void)
{
	char *str;
	char cmdline[1024] = {0};
	char tmpbuf[128] = {0};
	str = getenv("bootargs");

	strcpy(cmdline,str);
	//charge type
	if(gd->chargemode)
	{
		if((0==strcmp(getenv("bootcmd"),"run setargs_mmc boot_normal"))||(0==strcmp(getenv("bootcmd"),"run setargs_nand boot_normal")))
		{
			printf("only in boot normal mode, pass charger para to kernel\n");
			strcat(cmdline," androidboot.mode=charger");
		}
	}
	//serial info
	str = getenv("sunxi_serial");
	sprintf(tmpbuf," androidboot.serialno=%s",str);
	strcat(cmdline,tmpbuf);
	//harware info
	sprintf(tmpbuf," androidboot.hardware=%s",board_hardware_info());
	strcat(cmdline,tmpbuf);

	setenv("bootargs", cmdline);
}
int do_boota (cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{

	ulong os_load_addr;
	ulong os_data = 0,os_len = 0;
	ulong rd_data,rd_len;
	struct  andr_img_hdr *fb_hdr = NULL;
	void *dtb_base = (void*)CONFIG_SUNXI_FDT_ADDR;

	if (argc < 2)
		return cmd_usage(cmdtp);

	os_load_addr = simple_strtoul(argv[1], NULL, 16);
	fb_hdr = (struct andr_img_hdr *)os_load_addr;

	if(android_image_check_header(fb_hdr))
	{
		puts("boota: bad boot image magic, maybe not a boot.img?\n");
		return -1;
	}
	android_image_get_kernel(fb_hdr,0,&os_data,&os_len);
	android_image_get_ramdisk(fb_hdr,&rd_data,&rd_len);

	memcpy2((void*) (long)fb_hdr->kernel_addr, (const void *)os_data, os_len);
	memcpy2((void*) (long)fb_hdr->ramdisk_addr, (const void *)rd_data, rd_len);
#ifdef CONFIG_SUNXI_SECURE_SYSTEM
	if(gd->securemode)
	{
		ulong total_len = ALIGN(fb_hdr->ramdisk_size, fb_hdr->page_size) + 	\
		                  ALIGN(fb_hdr->kernel_size, fb_hdr->page_size)  +  \
		                  fb_hdr->page_size;

		printf("total_len=%d\n", (unsigned int)total_len);
		//为了签名检查，必须知道当前启动的分区名称
		int ret = sunxi_verify_signature((void *)os_load_addr, (unsigned int)total_len, argv[2]);
		if(ret)
		{
			printf("boota: verify the %s failed\n", argv[2]);

			return 1;
		}
	}
#endif
#ifdef SYS_CONFIG_MEMBASE
	debug("moving sysconfig.bin from %lx to: %lx, size 0x%lx\n",
		(ulong)gd->script_reloc_buf,
		(ulong)(SYS_CONFIG_MEMBASE),
		gd->script_reloc_size);

	memcpy((void*)SYS_CONFIG_MEMBASE, (void*)gd->script_reloc_buf,gd->script_reloc_size);
#endif
	update_bootargs();

	//update fdt bootargs from env config
	fdt_chosen(working_fdt);
	fdt_initrd(working_fdt,(ulong)fb_hdr->ramdisk_addr, (ulong)(fb_hdr->ramdisk_addr+rd_len));
	debug("moving platform.dtb from %lx to: %lx, size 0x%lx\n",
		(ulong)dtb_base,
		(ulong)(gd->fdt_blob),gd->fdt_size);
	
	#if !defined(CONFIG_ARCH_SUNIVW1P1)
	//fdt_blob  save the addree of  working_fdt
	memcpy((void*)dtb_base, gd->fdt_blob,gd->fdt_size);	
	 
	
	if(fdt_check_header(dtb_base) !=0 )
	{
		printf("fdt header check error befor boot\n");
		return -1;
	}
	#endif

	tick_printf("ready to boot\n");
#if 1
	#if defined(CONFIG_ARCH_SUNIVW1P1)
	do_boota_linux(fb_hdr, (void*)gd->fdt_blob);
	#else
	do_boota_linux(fb_hdr, dtb_base);
	#endif
	
	puts("Boot linux failed, control return to monitor\n");
#else
	puts("Boot linux test ok, control return to monitor\n");
#endif

	return 0;
}

U_BOOT_CMD(
	boota,	3,	1,	do_boota,
	"boota   - boot android bootimg from memory\n",
	"<addr>\n    - boot application image stored in memory\n"
	"\t'addr' should be the address of boot image which is kernel+ramdisk.img\n"
);
#endif

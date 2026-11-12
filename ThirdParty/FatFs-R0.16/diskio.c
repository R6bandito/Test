/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* 移植接口先空着。不碰 SDIO，调用一律未就绪。 */

#include "ff.h"
#include "diskio.h"


DSTATUS disk_initialize (BYTE pdrv)
{
	(void)pdrv;
	return STA_NOINIT;
}


DSTATUS disk_status (BYTE pdrv)
{
	(void)pdrv;
	return STA_NOINIT;
}


DRESULT disk_read (BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
	(void)pdrv;
	(void)buff;
	(void)sector;
	(void)count;
	return RES_NOTRDY;
}


DRESULT disk_write (BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
	(void)pdrv;
	(void)buff;
	(void)sector;
	(void)count;
	return RES_NOTRDY;
}


DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void *buff)
{
	(void)pdrv;
	(void)cmd;
	(void)buff;
	return RES_NOTRDY;
}

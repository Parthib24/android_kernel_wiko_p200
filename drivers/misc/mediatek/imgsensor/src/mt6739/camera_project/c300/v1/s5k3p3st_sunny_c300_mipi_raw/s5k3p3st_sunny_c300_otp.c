/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*
 * Driver for CAM_CAL
 *
 *
 */

#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include "cam_cal.h"
#include <linux/module.h>
#include <linux/videodev2.h>
#include <linux/atomic.h>

#include "kd_camera_typedef.h"
#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_imgsensor_errcode.h"

/* #include <asm/system.h>  // for SMP */
#if 1
typedef struct stCAM_CAL_INFO_STRUCT {
        u32 u4Offset;
        u32 u4Length;
        u32 sensorID;
        u32 deviceID;/* MAIN = 0x01, SUB  = 0x02, MAIN_2 = 0x04, SUB_2 = 0x08 */
        u8 *pu1Params;
}stCAM_CAL_INFO_STRUCT;
#endif

/* #define CAM_CALGETDLT_DEBUG //test */
 #define CAM_CAL_DEBUG
#ifdef CAM_CAL_DEBUG
#define CAM_CALDB pr_debug
#else
#define CAM_CALDB(x, ...)
#endif

static DEFINE_SPINLOCK(g_CAM_CALLock); /* for SMP */
//#define CAM_CAL_I2C_BUSNUM 1
//static struct i2c_board_info kd_cam_cal_dev __initdata = { I2C_BOARD_INFO("dummy_cam_cal", 0xAB >> 1)};
/* make s5k3p3st_sunny_c300_eeprom co-exist */

/*******************************************************************************
*
********************************************************************************/
#define CAM_CAL_ICS_REVISION 1 /* seanlin111208 */
#define CAM_CAL_DEV_MAJOR_NUMBER 226

/* CAM_CAL READ/WRITE ID */
//#define S24CS64A_DEVICE_ID 0xAB/*0xFE */
/*******************************************************************************
*
********************************************************************************/
#define CAM_CAL_DRVNAME "S5K3P3ST_SUNNY_C300_CAM_CAL_DRV"
#define CAM_CAL_I2C_GROUP_ID 0
/*******************************************************************************
*
********************************************************************************/
/* add for linux-4.4 */
#if 0
#ifndef I2C_WR_FLAG
#define I2C_WR_FLAG     (0x1000)
#define I2C_MASK_FLAG	(0x00ff)
#endif
#endif
//static struct i2c_client *g_pstI2Cclient;

/* 81 is used for V4L driver */
static dev_t g_CAM_CALdevno = MKDEV(CAM_CAL_DEV_MAJOR_NUMBER, 0);
static struct cdev *g_pCAM_CAL_CharDrv;
/* static spinlock_t g_CAM_CALLock; */
static struct class *CAM_CAL_class;
static atomic_t g_CAM_CALatomic;

#if 0
/*******************************************************************************
 *
 ********************************************************************************/
/* maximun read length is limited at "I2C_FIFO_SIZE" in I2c-mt65xx.c which is 8 bytes */
int iWriteCAM_CAL(u16 a_u2Addr, u32 a_u4Bytes, u8 *puDataInBytes)
{
	int  i4RetValue = 0;
	u32 u4Index = 0;
	char puSendCmd[8] = {(char)(a_u2Addr >> 8), (char)(a_u2Addr & 0xFF),
		0, 0, 0, 0, 0, 0
	};
	if (a_u4Bytes + 2 > 8) {
		CAM_CALDB("[CAM_CAL] exceed I2c-mt65xx.c 8 bytes limitation (include address 2 Byte)\n");
		return -1;
	}

	for (u4Index = 0; u4Index < a_u4Bytes; u4Index += 1)
		puSendCmd[(u4Index + 2)] = puDataInBytes[u4Index];

	i4RetValue = i2c_master_send(g_pstI2Cclient, puSendCmd, (a_u4Bytes + 2));
	if (i4RetValue != (a_u4Bytes + 2)) {
		CAM_CALDB("[CAM_CAL] I2C write  failed!!\n");
		return -1;
	}
	mdelay(10); /* for tWR singnal --> write data form buffer to memory. */

	/* CAM_CALDB("[CAM_CAL] iWriteCAM_CAL done!!\n"); */
	return 0;
}

/* maximun read length is limited at "I2C_FIFO_SIZE" in I2c-mt65xx.c which is 8 bytes */
int iReadCAM_CAL(u16 a_u2Addr, u32 ui4_length, u8 *a_puBuff)
{
	int  i4RetValue = 0;
	char puReadCmd[2] = {(char)(a_u2Addr >> 8), (char)(a_u2Addr & 0xFF)};

	/* CAM_CALDB("[CAM_CAL] iReadCAM_CAL!!\n"); */

	if (ui4_length > 8) {
		CAM_CALDB("[CAM_CAL] exceed I2c-mt65xx.c 8 bytes limitation\n");
		return -1;
	}
	spin_lock(&g_CAM_CALLock); /* for SMP */
	g_pstI2Cclient->addr = g_pstI2Cclient->addr & (I2C_MASK_FLAG | I2C_WR_FLAG);
	spin_unlock(&g_CAM_CALLock); /* for SMP */

	/* CAM_CALDB("[EERPOM] i2c_master_send\n"); */
	i4RetValue = i2c_master_send(g_pstI2Cclient, puReadCmd, 2);
	if (i4RetValue != 2) {
		CAM_CALDB("[CAM_CAL] I2C send read address failed!!\n");
		return -1;
	}

	/* CAM_CALDB("[EERPOM] i2c_master_recv\n"); */
	i4RetValue = i2c_master_recv(g_pstI2Cclient, (char *)a_puBuff, ui4_length);
	if (i4RetValue != ui4_length) {
		CAM_CALDB("[CAM_CAL] I2C read data failed!!\n");
		return -1;
	}
	spin_lock(&g_CAM_CALLock); /* for SMP */
	g_pstI2Cclient->addr = g_pstI2Cclient->addr & I2C_MASK_FLAG;
	spin_unlock(&g_CAM_CALLock); /* for SMP */

	/* CAM_CALDB("[CAM_CAL] iReadCAM_CAL done!!\n"); */
	return 0;
}

static int iWriteData(unsigned int  ui4_offset, unsigned int  ui4_length, unsigned char *pinputdata)
{
	int  i4RetValue = 0;
	int  i4ResidueDataLength;
	u32 u4IncOffset = 0;
	u32 u4CurrentOffset;
	u8 *pBuff;

	CAM_CALDB("[CAM_CAL] iWriteData\n");

	if (ui4_offset + ui4_length >= 0x2000) {
		CAM_CALDB("[CAM_CAL] Write Error!! S-24CS64A not supprt address >= 0x2000!!\n");
		return -1;
	}

	i4ResidueDataLength = (int)ui4_length;
	u4CurrentOffset = ui4_offset;
	pBuff = pinputdata;
	CAM_CALDB("[CAM_CAL] iWriteData u4CurrentOffset is %d\n", u4CurrentOffset);
	do {
		if (i4ResidueDataLength >= 6) {
			i4RetValue = iWriteCAM_CAL((u16)u4CurrentOffset, 6, pBuff);
			if (i4RetValue != 0) {
				CAM_CALDB("[CAM_CAL] I2C iWriteData failed!!\n");
				return -1;
			}
			u4IncOffset += 6;
			i4ResidueDataLength -= 6;
			u4CurrentOffset = ui4_offset + u4IncOffset;
			pBuff = pinputdata + u4IncOffset;
		} else {
			i4RetValue = iWriteCAM_CAL((u16)u4CurrentOffset, i4ResidueDataLength, pBuff);
			if (i4RetValue != 0) {
				CAM_CALDB("[CAM_CAL] I2C iWriteData failed!!\n");
				return -1;
			}
			u4IncOffset += 6;
			i4ResidueDataLength -= 6;
			u4CurrentOffset = ui4_offset + u4IncOffset;
			pBuff = pinputdata + u4IncOffset;
			/* break; */
		}
	} while (i4ResidueDataLength > 0);
	CAM_CALDB("[CAM_CAL] iWriteData done\n");

	return 0;
}

/* int iReadData(stCAM_CAL_INFO_STRUCT * st_pOutputBuffer) */
static int iReadData(unsigned int  ui4_offset, unsigned int  ui4_length, unsigned char *pinputdata)
{
	int  i4RetValue = 0;
	int  i4ResidueDataLength;
	u32 u4IncOffset = 0;
	u32 u4CurrentOffset;
	u8 *pBuff;

	CAM_CALDB("[CAM_CAL] iReadData\n");

	if (ui4_offset + ui4_length >= 0x2000) {
		CAM_CALDB("[CAM_CAL] Read Error!! S-24CS64A not supprt address >= 0x2000!!\n");
		return -1;
	}

	i4ResidueDataLength = (int)ui4_length;
	u4CurrentOffset = ui4_offset;
	pBuff = pinputdata;
	do {
		if (i4ResidueDataLength >= 8) {
			i4RetValue = iReadCAM_CAL((u16)u4CurrentOffset, 8, pBuff);
			if (i4RetValue != 0) {
				CAM_CALDB("[CAM_CAL] I2C iReadData failed!!\n");
				return -1;
			}
			u4IncOffset += 8;
			i4ResidueDataLength -= 8;
			u4CurrentOffset = ui4_offset + u4IncOffset;
			pBuff = pinputdata + u4IncOffset;
		} else {
			i4RetValue = iReadCAM_CAL((u16)u4CurrentOffset, i4ResidueDataLength, pBuff);
			if (i4RetValue != 0) {
				CAM_CALDB("[CAM_CAL] I2C iReadData failed!!\n");
				return -1;
			}
			u4IncOffset += 8;
			i4ResidueDataLength -= 8;
			u4CurrentOffset = ui4_offset + u4IncOffset;
			pBuff = pinputdata + u4IncOffset;
			/* break; */
		}
	} while (i4ResidueDataLength > 0);


	CAM_CALDB("[CAM_CAL] iReadData done\n");
	return 0;
}
#endif

extern kal_uint8 s5k3p3st_sunny_c300_lsc_data[1868];
u8 S5K3P3ST_SUNNY_C300_CheckID[]= {0x10,0xff,0x00,0x40,0x89};

static int selective_read_region(u32 offset, unsigned char* data,u16 i2c_id,u32 size)
{    	
	CAM_CALDB("[GC8034_CAM_CAL] selective_read_region offset =%d size %d data read = %d\n", offset,size, *data);

	if(size == 1868 ){
			memcpy((void *)data,(void *)&s5k3p3st_sunny_c300_lsc_data[0],size);
	}
	if(size == 4){
	memcpy((void *)data,(void *)&S5K3P3ST_SUNNY_C300_CheckID[1],size);
	}

    return 0;//NEEDED!!!
}
/********************************************************************************/
#define NEW_UNLOCK_IOCTL
#ifndef NEW_UNLOCK_IOCTL
static int CAM_CAL_Ioctl(struct inode *a_pstInode,
		struct file *a_pstFile,
		unsigned int a_u4Command,
		unsigned long a_u4Param)
#else
static long CAM_CAL_Ioctl(
		struct file *file,
		unsigned int a_u4Command,
		unsigned long a_u4Param
		)
#endif
{
	int i4RetValue = 0;
	u8 *pBuff = NULL;
	u8 *pWorkingBuff = NULL;
	stCAM_CAL_INFO_STRUCT *ptempbuf;

	#ifdef CAM_CALGETDLT_DEBUG
	struct timeval ktv1, ktv2;
	unsigned long TimeIntervalUS;
	#endif
	if (_IOC_DIR(a_u4Command) != _IOC_NONE) {
		pBuff = kmalloc(sizeof(stCAM_CAL_INFO_STRUCT), GFP_KERNEL);

		if (pBuff == NULL) {
			CAM_CALDB("[CAM_CAL] ioctl allocate mem failed\n");
			return -ENOMEM;
		}

		if (_IOC_WRITE & _IOC_DIR(a_u4Command)) {
			if (copy_from_user((u8 *) pBuff, (u8 *) a_u4Param, sizeof(stCAM_CAL_INFO_STRUCT))) {
				/* get input structure address */
				kfree(pBuff);
				CAM_CALDB("[CAM_CAL] ioctl copy from user failed\n");
				return -EFAULT;
			}
		}
	}

	ptempbuf = (stCAM_CAL_INFO_STRUCT *)pBuff;
	pWorkingBuff = kmalloc(ptempbuf->u4Length, GFP_KERNEL);
	if (pWorkingBuff == NULL) {
		kfree(pBuff);
		CAM_CALDB("[CAM_CAL] ioctl allocate mem failed\n");
		return -ENOMEM;
	}

	if (copy_from_user((u8 *)pWorkingBuff, (u8 *)ptempbuf->pu1Params, ptempbuf->u4Length)) {
		kfree(pBuff);
		kfree(pWorkingBuff);
		CAM_CALDB("[CAM_CAL] ioctl copy from user failed\n");
		return -EFAULT;
	}

	switch (a_u4Command) {
		case CAM_CALIOC_S_WRITE:
			CAM_CALDB("[CAM_CAL] Write CMD\n");
			#ifdef CAM_CALGETDLT_DEBUG
			do_gettimeofday(&ktv1);
			#endif
			//i4RetValue = iWriteData((u16)ptempbuf->u4Offset, ptempbuf->u4Length, pWorkingBuff);
			#ifdef CAM_CALGETDLT_DEBUG
			do_gettimeofday(&ktv2);
			if (ktv2.tv_sec > ktv1.tv_sec)
				TimeIntervalUS = ktv1.tv_usec + 1000000 - ktv2.tv_usec;
			else
				TimeIntervalUS = ktv2.tv_usec - ktv1.tv_usec;

			CAM_CALDB("Write data %d bytes take %lu us\n", ptempbuf->u4Length, TimeIntervalUS);
			#endif
			break;
		case CAM_CALIOC_G_READ:
			printk("[CAM_CAL] Read CMD\n");
			#ifdef CAM_CALGETDLT_DEBUG
			do_gettimeofday(&ktv1);
			#endif
			printk("[CAM_CAL] offset %d\n", ptempbuf->u4Offset);
			printk("[CAM_CAL] length %d\n", ptempbuf->u4Length);

			//i4RetValue = iReadData((u16)ptempbuf->u4Offset, ptempbuf->u4Length, pWorkingBuff);
			if(ptempbuf->u4Length == 1868)
			{			
				 memcpy(pWorkingBuff,s5k3p3st_sunny_c300_lsc_data,ptempbuf->u4Length);
			}
			else if(ptempbuf->u4Length == 4)
			{				
				i4RetValue = selective_read_region(ptempbuf->u4Offset, pWorkingBuff, 0x20, ptempbuf->u4Length);		
			}    
			#ifdef CAM_CALGETDLT_DEBUG
			do_gettimeofday(&ktv2);
			if (ktv2.tv_sec > ktv1.tv_sec)
				TimeIntervalUS = ktv1.tv_usec + 1000000 - ktv2.tv_usec;
			else
				TimeIntervalUS = ktv2.tv_usec - ktv1.tv_usec;

			CAM_CALDB("Read data %d bytes take %lu us\n", ptempbuf->u4Length, TimeIntervalUS);
			#endif

			break;
		default:
			CAM_CALDB("[CAM_CAL] No CMD\n");
			i4RetValue = -EPERM;
			break;
	}
	if (_IOC_READ & _IOC_DIR(a_u4Command)) {
		/* copy data to user space buffer, keep other input paremeter unchange. */
		CAM_CALDB("[CAM_CAL] to user length %d\n", ptempbuf->u4Length);


		if (copy_to_user((u8 __user *) ptempbuf->pu1Params, (u8 *)pWorkingBuff, ptempbuf->u4Length)) {
			kfree(pBuff);
			kfree(pWorkingBuff);
			CAM_CALDB("[CAM_CAL] ioctl copy to user failed\n");
			return -EFAULT;
		}
	}

	kfree(pBuff);
	kfree(pWorkingBuff);
	printk("s5k3p3st_sunny_c300_otp [CAM_CAL] %d\n",i4RetValue);
	return i4RetValue;
}

static u32 g_u4Opened;
/* #define */
/* Main jobs: */
/* 1.check for device-specified errors, device not ready. */
/* 2.Initialize the device if it is opened for the first time. */
static int CAM_CAL_Open(struct inode *a_pstInode, struct file *a_pstFile)
{
	int ret = 0;

	CAM_CALDB("[S24CAM_CAL] CAM_CAL_Open\n");
	spin_lock(&g_CAM_CALLock);
	if (g_u4Opened) {
		ret = -EBUSY;
	} else {
		g_u4Opened = 1;
		atomic_set(&g_CAM_CALatomic, 0);
		ret = 0;
	}
	spin_unlock(&g_CAM_CALLock);

	/* #if defined(MT6572) */
	/* do nothing */
	/* #else */
	/* if(TRUE != hwPowerOn(MT65XX_POWER_LDO_VCAMA, VOL_2800, "S24CS64A")) */
	/* { */
	/* CAM_CALDB("[CAM_CAL] Fail to enable analog gain\n"); */
	/* return -EIO; */
	/* } */
	/* #endif */

	return ret;
}

/* Main jobs: */
/* 1.Deallocate anything that "open" allocated in private_data. */
/* 2.Shut down the device on last close. */
/* 3.Only called once on last time. */
/* Q1 : Try release multiple times. */
static int CAM_CAL_Release(struct inode *a_pstInode, struct file *a_pstFile)
{
	spin_lock(&g_CAM_CALLock);

	g_u4Opened = 0;

	atomic_set(&g_CAM_CALatomic, 0);

	spin_unlock(&g_CAM_CALLock);

	return 0;
}

static const struct file_operations g_stCAM_CAL_fops = {
	.owner = THIS_MODULE,
	.open = CAM_CAL_Open,
	.release = CAM_CAL_Release,
	/* .ioctl = CAM_CAL_Ioctl */
	.unlocked_ioctl = CAM_CAL_Ioctl
};

#define CAM_CAL_DYNAMIC_ALLOCATE_DEVNO 1
static inline int RegisterCAM_CALCharDrv(void)
{
	struct device *CAM_CAL_device = NULL;

	#if CAM_CAL_DYNAMIC_ALLOCATE_DEVNO
	if (alloc_chrdev_region(&g_CAM_CALdevno, 0, 1, CAM_CAL_DRVNAME)) {
		CAM_CALDB("[CAM_CAL] Allocate device no failed\n");

		return -EAGAIN;
	}
	#else
	if (register_chrdev_region(g_CAM_CALdevno, 1, CAM_CAL_DRVNAME)) {
		CAM_CALDB("[CAM_CAL] Register device no failed\n");

		return -EAGAIN;
	}
	#endif

	/* Allocate driver */
	g_pCAM_CAL_CharDrv = cdev_alloc();

	if (g_pCAM_CAL_CharDrv == NULL) {
		unregister_chrdev_region(g_CAM_CALdevno, 1);

		CAM_CALDB("[CAM_CAL] Allocate mem for kobject failed\n");

		return -ENOMEM;
	}

	/* Attatch file operation. */
	cdev_init(g_pCAM_CAL_CharDrv, &g_stCAM_CAL_fops);

	g_pCAM_CAL_CharDrv->owner = THIS_MODULE;

	/* Add to system */
	if (cdev_add(g_pCAM_CAL_CharDrv, g_CAM_CALdevno, 1)) {
		CAM_CALDB("[CAM_CAL] Attatch file operation failed\n");
		unregister_chrdev_region(g_CAM_CALdevno, 1);

		return -EAGAIN;
	}

	CAM_CAL_class = class_create(THIS_MODULE, "S5K3P3ST_SUNNY_C300_CAL_DRV");
	if (IS_ERR(CAM_CAL_class)) {
		int ret = PTR_ERR(CAM_CAL_class);

		CAM_CALDB("Unable to create class, err = %d\n", ret);
		return ret;
	}
	CAM_CAL_device = device_create(CAM_CAL_class, NULL, g_CAM_CALdevno, NULL, CAM_CAL_DRVNAME);

	return 0;
}

static inline void UnregisterCAM_CALCharDrv(void)
{
	/* Release char driver */
	cdev_del(g_pCAM_CAL_CharDrv);

	unregister_chrdev_region(g_CAM_CALdevno, 1);

	device_destroy(CAM_CAL_class, g_CAM_CALdevno);
	class_destroy(CAM_CAL_class);
}
#if 0
/* //////////////////////////////////////////////////////////////////// */
#ifndef CAM_CAL_ICS_REVISION
static int CAM_CAL_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info);
#elif 0
static int CAM_CAL_i2c_detect(struct i2c_client *client, struct i2c_board_info *info);
#else
#endif
static int CAM_CAL_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int CAM_CAL_i2c_remove(struct i2c_client *);

static const struct i2c_device_id CAM_CAL_i2c_id[] = {{CAM_CAL_DRVNAME, 0}, {} };
#if 0 /* test110314 Please use the same I2C Group ID as Sensor */
static unsigned short force[] = {CAM_CAL_I2C_GROUP_ID, S24CS64A_DEVICE_ID, I2C_CLIENT_END, I2C_CLIENT_END};
#else
/* static unsigned short force[] = {CAM_CAL_I2C_GROUP_ID, S24CS64A_DEVICE_ID, I2C_CLIENT_END, I2C_CLIENT_END}; */
#endif
/* static const unsigned short * const forces[] = { force, NULL }; */
/* static struct i2c_client_address_data addr_data = { .forces = forces,}; */


static struct i2c_driver CAM_CAL_i2c_driver = {
	.probe = CAM_CAL_i2c_probe,
	.remove = CAM_CAL_i2c_remove,
	/* .detect = CAM_CAL_i2c_detect, */
	.driver.name = CAM_CAL_DRVNAME,
	.id_table = CAM_CAL_i2c_id,
};

#ifndef CAM_CAL_ICS_REVISION
static int CAM_CAL_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info)
{
	strcpy(info->type, CAM_CAL_DRVNAME);
	return 0;
}
#endif

static int CAM_CAL_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int i4RetValue = 0;

	CAM_CALDB("[CAM_CAL] Attach I2C\n");
	/* spin_lock_init(&g_CAM_CALLock); */

	/* get sensor i2c client */
	spin_lock(&g_CAM_CALLock); /* for SMP */
	g_pstI2Cclient = client;
	g_pstI2Cclient->addr = S24CS64A_DEVICE_ID >> 1;
	spin_unlock(&g_CAM_CALLock); /* for SMP */

	CAM_CALDB("[CAM_CAL] g_pstI2Cclient->addr = 0x%8x\n", g_pstI2Cclient->addr);

	/* Register char driver */
	i4RetValue = RegisterCAM_CALCharDrv();

	if (i4RetValue) {
		CAM_CALDB("[CAM_CAL] register char device failed!\n");
		return i4RetValue;
	}


	CAM_CALDB("[CAM_CAL] Attached!!\n");
	return 0;
}

static int CAM_CAL_i2c_remove(struct i2c_client *client)
{
	return 0;
}
#endif
static int CAM_CAL_probe(struct platform_device *pdev)
{
	//return i2c_add_driver(&CAM_CAL_i2c_driver);
	return 0;
}

static int CAM_CAL_remove(struct platform_device *pdev)
{
	//i2c_del_driver(&CAM_CAL_i2c_driver);
	return 0;
}

/* platform structure */
static struct platform_driver g_stCAM_CAL_Driver = {
	.probe      = CAM_CAL_probe,
	.remove     = CAM_CAL_remove,
	.driver     = {
		.name   = CAM_CAL_DRVNAME,
		.owner  = THIS_MODULE,
	}
};

static struct platform_device g_stCAM_CAL_Device = {
	.name = CAM_CAL_DRVNAME,
	.id = 0,
	.dev = {
	}
};

static int __init S5K3P3ST_SUNNY_C300_CAM_CAL_init(void)
{
	//i2c_register_board_info(CAM_CAL_I2C_BUSNUM, &kd_cam_cal_dev, 1);
	int i4RetValue = 0;
	CAM_CALDB("GC8034_CAM_CAL]\n");
	
	/* Register char driver */
	i4RetValue = RegisterCAM_CALCharDrv();
	if (i4RetValue) {
		CAM_CALDB("[CAM_CAL] register char device failed!\n");
		return i4RetValue;
	}
	CAM_CALDB("[GC8034_CAM_CAL] Attached!! \n");

	if(platform_driver_register(&g_stCAM_CAL_Driver)){
		CAM_CALDB("failed to register CAM_CAL driver\n");
		return -ENODEV;
	}

	if (platform_device_register(&g_stCAM_CAL_Device)) {
		CAM_CALDB("failed to register CAM_CAL driver\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit S5K3P3ST_SUNNY_C300_CAM_CAL_exit(void)
{
	platform_driver_unregister(&g_stCAM_CAL_Driver);
}

module_init(S5K3P3ST_SUNNY_C300_CAM_CAL_init);
module_exit(S5K3P3ST_SUNNY_C300_CAM_CAL_exit);

MODULE_DESCRIPTION("S5K3P3ST_SUNNY_C300_CAM_CAL driver");
MODULE_AUTHOR("tinno");
MODULE_LICENSE("GPL");

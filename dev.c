#include <linux/delay.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <asm/uaccess.h>

#ifndef MEMDEV_MAJOR
#define MEMDEV_MAJOR 250
#endif

#ifndef MEMDEV_NR_DEVS
#define MEMDEV_NR_DEVS 1
#endif

#ifndef MEMDEV_SIZE
#define MEMDEV_SIZE 4096
#endif

//---------------------------------------

MODULE_LICENSE("GPL");

struct yinwoods_data {
    char a[4096];
    char *name;

    int left;   //算式左侧运算数
    int right;  //算式右侧运算数
    int result; //算式运算结果
    
    int status; //进程状态位

    struct cdev cdev;

    char *mutex;//进程间交互变量
};

struct yinwoods_data *p;

static void yinwoods_release(struct device *dev) {
    return;
}

struct yinwoods_data info = {
    .name = "yinwoods_zero",
    //主
    .status = 0,
    .left = 1,
    .right = 0,
    .mutex = "translate this message from info",
};

static struct platform_device yinwoods_device = {
    .name = "yinwoods",
    .id = 0,
    .dev = {
        .platform_data = &info,
        .release = yinwoods_release,
    },
};

//---------------------------------------

struct mem_dev {
    char *data;
    unsigned long size;
};

static int mem_major = MEMDEV_MAJOR;

module_param(mem_major, int, S_IRUGO);

struct mem_dev *mem_devp;

int mem_open(struct inode *inode, struct file *filp) {
    struct mem_dev *dev;

    int num = MINOR(inode->i_rdev);

    if(num >= MEMDEV_NR_DEVS)
        return -ENODEV;
    dev = &mem_devp[num];

    filp->private_data = dev;

    return 0;
}

int mem_release(struct inode *inode, struct file *filp) {
    return 0;
}

static ssize_t mem_read(struct file *filp, char __user *buf, size_t size, loff_t *ppos) {
    unsigned long p = *ppos;
    unsigned int count = size;
    int ret = 0;
    struct mem_dev *dev = filp->private_data;

    if(p >= MEMDEV_SIZE)
        return 0;
    if(count > MEMDEV_SIZE - p)
        count = MEMDEV_SIZE - p;

    if(copy_to_user(buf, (void *)(dev->data + p), count))
        ret = -EFAULT;
    else {
        *ppos += count;
        ret = count;
        printk(KERN_INFO "read %u byte(s) from %lu\n", count, p);
    }
    return ret;
}

static ssize_t mem_write(struct file *filp, const char __user *buf, size_t size, loff_t *ppos) {
    unsigned long p = *ppos;
    unsigned int count = size;
    int ret = 0;
    struct mem_dev *dev = filp->private_data;

    if(p >= MEMDEV_SIZE)
        return 0;

    if(count > MEMDEV_SIZE - p)
        count = MEMDEV_SIZE - p;

    if(copy_from_user(dev->data + p, buf, count))
        ret = -EFAULT;
    else {
        *ppos += count;
        ret = count;
        printk(KERN_INFO "written %u byte(s) from %lu\n", count, p);
    }
    return ret;
}

static loff_t mem_llseek(struct file *filp, loff_t offset, int whence) {
    loff_t newpos;

    switch(whence) {
        case 0:
            newpos = offset;
            break;
            
        case 1:
            newpos = filp->f_pos + offset;
            break;

        case 2:
            newpos = MEMDEV_SIZE - 1 + offset;
            break;

        default:
            return -EINVAL;
    }

    if((newpos < 0) || (newpos > MEMDEV_SIZE))
        return -EINVAL;

    filp->f_pos = newpos;
    return newpos;
}

static const struct file_operations mem_fops = {
    .owner = THIS_MODULE,
    .llseek = mem_llseek,
    .read = mem_read,
    .write = mem_write,
    .open = mem_open,
    .release = mem_release,
};



static int __init yinwoods_init(void) {

    int result;
    int i;

    dev_t devno = MKDEV(mem_major, 0);

    if(mem_major)
        result = register_chrdev_region(devno, 2, "memdev");
    else {
        result = alloc_chrdev_region(&devno, 0, 2, "memdev");
        mem_major = MAJOR(devno);
    }

    if(result < 0)
        return result;

    cdev_init(&info.cdev, &mem_fops);
    info.cdev.owner = THIS_MODULE;
    info.cdev.ops = &mem_fops;

    cdev_add(&info.cdev, MKDEV(mem_major, 0), MEMDEV_NR_DEVS);

    mem_devp = kmalloc(MEMDEV_NR_DEVS * sizeof(struct mem_dev), GFP_KERNEL);

    if(!mem_devp) {
        result = -ENOMEM;
        goto fail_malloc;
    }
    memset(mem_devp, 0, sizeof(struct mem_dev));

    for(i=0; i<MEMDEV_NR_DEVS; ++i) {
        mem_devp[i].size = MEMDEV_SIZE;
        mem_devp[i].data = kmalloc(MEMDEV_SIZE, GFP_KERNEL);
        memset(mem_devp[i].data, 0, MEMDEV_SIZE);
    }

    sprintf(info.a, "this message is from info0 to driver!");

    p = yinwoods_device.dev.platform_data;
    p->status = 0;
    platform_device_register(&yinwoods_device);


    //platform_add_devices(yinwoods_device, ARRAY_SIZE(yinwoods_device));
    return 0;

    fail_malloc:
        unregister_chrdev_region(devno, 1);
    return result;
}

static void __exit yinwoods_exit(void) {

    cdev_del(&info.cdev);
    kfree(mem_devp);
    unregister_chrdev_region(MKDEV(mem_major, 0), 2);

    platform_device_unregister(&yinwoods_device);
}

module_init(yinwoods_init);
module_exit(yinwoods_exit);

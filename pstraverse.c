#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/device.h>

#define SIZE 100
static char *memoryBlock;

dev_t dev = 0;
static struct class *dev_class;
static struct cdev my_cdev;

static char *option = "-d";
static int pid = 1;

static struct pid *pid_struct;
static struct task_struct *task;

module_param(option, charp, 0);
module_param(pid, int, 0);

static int __init my_init(void);
static void __exit my_exit(void);
static int my_open(struct inode *inode, struct file *file);
static int my_release(struct inode *inode, struct file *file);
static ssize_t my_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t my_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static void DFS(struct task_struct *task);
static void BFS(struct task_struct *task);
static void BFS_helper(struct task_struct *task);

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = my_read,
    .write = my_write,
    .open = my_open,
    .release = my_release,
};

static LIST_HEAD(queue);

// Breadt First Search for process tree.
static void BFS(struct task_struct *task){
    struct task_struct *child;
    struct list_head *list;

    BFS_helper(task);

    printk(KERN_INFO "Name: %s PID: %d\n", task->comm, task->pid);
    list_for_each(list, &queue) {
        child = list_entry(list, struct task_struct, sibling);
        printk(KERN_INFO "Name: %s PID: %d\n", child->comm, child->pid);
    }
}

// Helper function for BFS.
static void BFS_helper(struct task_struct *task){
    struct task_struct *child;
    struct list_head *list;

    list_for_each(list, &task->children) {
        child = list_entry(list, struct task_struct, sibling);
        struct task_struct *curr = kmalloc(sizeof(struct task_struct), GFP_KERNEL);
        curr->pid = child->pid;
        strcpy(curr->comm, child->comm);
        INIT_LIST_HEAD(&curr->sibling);
        list_add_tail(&curr->sibling, &queue);
    }

    list_for_each(list, &task->children) {
        child = list_entry(list, struct task_struct, sibling);
        BFS_helper(child);
    }
}

// Depth First Search for process tree.
static void DFS(struct task_struct *task) {
    struct task_struct *child;
    struct list_head *list;

    printk(KERN_INFO "Name: %s PID: %d\n", task->comm, task->pid);
    list_for_each(list, &task->children) {
        child = list_entry(list, struct task_struct, sibling);
        DFS(child);
    }
}

// Space allocation for device file.
static int my_open(struct inode *inode, struct file *file) {
    memoryBlock = kmalloc(SIZE, GFP_KERNEL);
    return 0;
}

// Freeing allocated space.
static int my_release(struct inode *inode, struct file *file) {
    kfree(memoryBlock);
    return 0;
}

// Copy from kernel space to user space.
static ssize_t my_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    if (copy_to_user(buf, memoryBlock, strlen(memoryBlock)) != 0) {
        printk(KERN_INFO "Copy from kernel space to user space is failed.\n");
    }
    return len;
}

// Copy data from user space to kernel space and call the proper function.
static ssize_t my_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    if (copy_from_user(memoryBlock, buf, len) != 0) {
        printk(KERN_INFO "Copy from user space to kernel space is failed.\n");
    }

    char data[2][10];
    char *token;

    int count = 0;
    while ((token = strsep(&memoryBlock, " ")) != NULL) {
        strcpy(data[count], token);
        count++;
    }

    long pid2;
    if (kstrtol(data[0], 10, &pid2) == 0) {
        pid_struct = find_get_pid(pid2);
        task = pid_task(pid_struct, PIDTYPE_PID);

        if (task == NULL) {
            printk(KERN_INFO "PID does not exist.\n");
        } else {
            if(strcmp(data[1], "-d") == 0) {
                DFS(task);
            } else if (strcmp(data[1], "-b") == 0) {
                BFS(task);
            }
        }
    } 

    return len;
}

// Initilization function that creates the device file.
static int __init my_init(void) {
    printk(KERN_INFO "Inserting module pstraverse...\n");


    if((alloc_chrdev_region(&dev, 0, 1, "my_Dev")) < 0) {
        printk(KERN_INFO "Cannot allocate the major number...\n");
    }

    cdev_init(&my_cdev, &fops);

    if((cdev_add(&my_cdev, dev, 1)) < 0) {
        printk(KERN_INFO "Cannot add the device to the system...\n");
        goto r_class;
    }    

    if((dev_class = class_create(THIS_MODULE, "my_class")) == NULL) {
        printk(KERN_INFO "Cannot create the struct class...\n");
        goto r_class;
    }

    if((device_create(dev_class, NULL, dev, NULL, "my_device")) == NULL) {
        printk(KERN_INFO "Cannot create the device ..\n");
        goto r_device;
    }

    printk(KERN_INFO "Device driver insert...done properly...");

    pid_struct = find_get_pid(pid);
    task = pid_task(pid_struct, PIDTYPE_PID);

    if (task == NULL) {
        printk(KERN_INFO "PID does not exist.\n");
    } else {
        if(strcmp(option, "-d") == 0) {
            DFS(task);
        } else if (strcmp(option, "-b") == 0) {
            BFS(task);
        }
    }

    return 0;

r_device: 
    class_destroy(dev_class);

r_class:
    unregister_chrdev_region(dev, 1);
    return -1;
}

// Exit function that removes the device file.
static void __exit my_exit(void) {
    device_destroy(dev_class, dev);
    class_destroy(dev_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev, 1);

    printk(KERN_INFO "Module pstraverse is removed succesfully...\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kemal Bora Bayraktar");
MODULE_DESCRIPTION("The pstraverse command");

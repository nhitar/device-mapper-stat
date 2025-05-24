#include <linux/device-mapper.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>

#include <linux/atomic.h>
#include <linux/kobject.h>
#include <linux/slab.h>

#define DM_MSG_PREFIX "proxy"

static struct dm_stat {
    atomic64_t read_counter; // Счётчик количества запросов на чтение
    atomic64_t read_block_size_total; // Суммарный размер прочитанных данных
    atomic64_t write_counter; // Счётчик количества запросов на запись
    atomic64_t write_block_size_total; // Суммарный размер записанных данных
} stat;

struct proxy_data {
    struct dm_dev* dev; // Изначальное устройство
    char* origin_path; // Путь до изначального устройства
};

static struct kobject* stat_kobj;

/* Функция анализа структуры dm_stat и записи статистики для stat/volumes */
static ssize_t volumes_show(struct kobject* kobj, struct kobj_attribute* attr, char* buf) {
    long long read_count = atomic64_read(&stat.read_counter);
    long long write_count = atomic64_read(&stat.write_counter);
    long long total_count = read_count + write_count;

    long long read_block = atomic64_read(&stat.read_block_size_total);
    long long write_block = atomic64_read(&stat.write_block_size_total);
    long long total_block = read_block + write_block;

    long long read_avg = read_count ? read_block/read_count : 0;
    long long write_avg = write_count ? write_block/write_count : 0;
    long long total_avg = total_count ? total_block/total_count : 0;

    return snprintf(buf, PAGE_SIZE,
        "read:\n reqs: %lld\n avg size: %lld\n"
        "write:\n reqs: %lld\n avg size: %lld\n"
        "total:\n reqs: %lld\n avg size: %lld\n",
        read_count, read_avg,
        write_count, write_avg,
        total_count, total_avg);
}

static struct kobj_attribute volumes_attr = __ATTR_RO(volumes);

/* Атрибуты для sysfs */
static struct attribute_group attr_group = {
    .attrs = (struct attribute* []){ &volumes_attr.attr, NULL }
};

/* Функция-конструктор, вызываемая при dmsetup create для создания устройства */
static int proxy_ctr(struct dm_target* ti, unsigned int argc, char** argv) {
    if (argc != 1) {
        ti->error = "Arguments error";
        return -EINVAL;
    }
    
    struct proxy_data* pd = kmalloc(sizeof(*pd), GFP_KERNEL);
    pd->origin_path = kstrdup(argv[0], GFP_KERNEL);
    if (dm_get_device(ti, pd->origin_path, dm_table_get_mode(ti->table), &pd->dev)) {
        kfree(pd->origin_path);
        kfree(pd);
        ti->error = "Device check failed";
        return -EINVAL;
    }

    ti->private = pd;
    ti->num_discard_bios = 1;
    ti->discards_supported = true;
    
    return 0;
}

/* Функция, вызываемая при различных запросах BIO */
static int proxy_map(struct dm_target* ti, struct bio* bio) {
    printk(KERN_INFO "dmp: %s operation, size: %u, addr: %px\n",
        bio_op(bio) == REQ_OP_READ ? "read" : "write",
        bio->bi_iter.bi_size, bio); // Системные комментарии для dmesg

    switch (bio_op(bio)) {
        case REQ_OP_READ:
            if (bio->bi_opf & REQ_RAHEAD) {
                return DM_MAPIO_KILL;
            }
            atomic64_inc(&stat.read_counter);
            atomic64_add(bio->bi_iter.bi_size, &stat.read_block_size_total);
            break;
            
        case REQ_OP_WRITE:
            atomic64_inc(&stat.write_counter);
            atomic64_add(bio->bi_iter.bi_size, &stat.write_block_size_total);
            break;
            
        case REQ_OP_DISCARD:
            break;
            
        default:
            return DM_MAPIO_KILL;
    }

    struct proxy_data* pd = (struct proxy_data*)ti->private;
    bio_set_dev(bio, pd->dev->bdev);
    submit_bio_noacct(bio);
    return DM_MAPIO_SUBMITTED;
}

/* Дейструктор для target, вызываемый при удалении устройства */
static void proxy_dtr(struct dm_target* ti) {
    struct proxy_data* pd = ti->private;
    dm_put_device(ti, pd->dev);
    kfree(pd->origin_path);
    kfree(pd);
}

static struct target_type proxy_target = {
    .name = "dmp", // Имя типа target
    .version = {1, 0, 0}, // Версия
    .features = DM_TARGET_NOWAIT, // Флаги
    .module = THIS_MODULE, // Ссылка на модуль
    .ctr = proxy_ctr, // Конструктор (функция)
    .map = proxy_map, // Обработчик BIO (функция)
    .dtr = proxy_dtr, // Деструктор (функция)
};

/* Функция, вызываемая insmod для первичной инициализации */
static int __init dm_proxy_init(void) {
    atomic64_set(&stat.read_counter, 0);
    atomic64_set(&stat.write_counter, 0);
    atomic64_set(&stat.read_block_size_total, 0);
    atomic64_set(&stat.write_block_size_total, 0);

    stat_kobj = kobject_create_and_add("stat", &THIS_MODULE->mkobj.kobj);
    int ret = sysfs_create_group(stat_kobj, &attr_group);

    if (ret) {
        kobject_put(stat_kobj);
        return ret;
    }
    
    return dm_register_target(&proxy_target);
}

/* Функция, вызываемая rmmod для очистки при удалении */
static void __exit dm_proxy_exit(void) {
    sysfs_remove_group(stat_kobj, &attr_group);
    kobject_put(stat_kobj);
    dm_unregister_target(&proxy_target);
}

module_init(dm_proxy_init);
module_exit(dm_proxy_exit);

MODULE_AUTHOR("Nikita Pivoev <nikita.pivoev@yandex.ru>");
MODULE_DESCRIPTION(DM_NAME " stat proxy");
MODULE_LICENSE("GPL");

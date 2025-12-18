#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/time64.h>
#include <linux/version.h>

#define PROC_NAME "tsulab"

/*
 * Расстояние до Alpha Centauri примерно 4.37 световых года.
 * Переведём в секунды без float:
 * 4.37 = 437/100
 * 1 юлианский год = 31557600 секунд
 */
#define SECONDS_PER_JULIAN_YEAR 31557600LL
#define ALPHA_CENTAURI_TRAVEL_SEC ((437LL * SECONDS_PER_JULIAN_YEAR) / 100LL)

static struct proc_dir_entry *proc_entry;

static void format_utc(time64_t t, char *buf, size_t buflen)
{
	struct tm tm;
	time64_to_tm(t, 0, &tm); // UTC
	snprintf(buf, buflen, "%04ld-%02d-%02d %02d:%02d:%02d UTC",
	         tm.tm_year + 1900L, tm.tm_mon + 1, tm.tm_mday,
	         tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static int tsulab_show(struct seq_file *m, void *v)
{
	struct timespec64 now_ts;
	time64_t now, emitted, arrives;
	char now_s[32], emitted_s[32], arrives_s[32];

	ktime_get_real_ts64(&now_ts);
	now = now_ts.tv_sec;

	emitted = now - (time64_t)ALPHA_CENTAURI_TRAVEL_SEC;
	arrives  = now + (time64_t)ALPHA_CENTAURI_TRAVEL_SEC;

	format_utc(now, now_s, sizeof(now_s));
	format_utc(emitted, emitted_s, sizeof(emitted_s));
	format_utc(arrives, arrives_s, sizeof(arrives_s));

	seq_printf(m,
		"Alpha Centauri (approx): 4.37 light-years\n"
		"Light travel time (approx): %lld seconds (~4.37 years)\n\n"
		"Now (Earth, UTC): %s\n"
		"If we see Alpha Centauri light now, it left there at (Earth time, UTC): %s\n"
		"If a light ray leaves Alpha Centauri now, it will reach Earth at (Earth time, UTC): %s\n",
		(long long)ALPHA_CENTAURI_TRAVEL_SEC,
		now_s, emitted_s, arrives_s
	);

	return 0;
}

static int tsulab_open(struct inode *inode, struct file *file)
{
	return single_open(file, tsulab_show, NULL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops tsulab_ops = {
	.proc_open    = tsulab_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations tsulab_ops = {
	.open    = tsulab_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};
#endif

static int __init tsu_init(void)
{
	pr_info("Welcome to the Tomsk State University\n");

	proc_entry = proc_create(PROC_NAME, 0444, NULL, &tsulab_ops);
	if (!proc_entry) {
		pr_err("Failed to create /proc/%s\n", PROC_NAME);
		return -ENOMEM;
	}

	return 0;
}

static void __exit tsu_exit(void)
{
	if (proc_entry)
		proc_remove(proc_entry);

	pr_info("Tomsk State University forever!\n");
}

module_init(tsu_init);
module_exit(tsu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student");
MODULE_DESCRIPTION("TSU lab module with /proc/tsulab");

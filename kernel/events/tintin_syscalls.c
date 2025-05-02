#include <linux/fs.h>
 #include <linux/mm.h>
 #include <linux/cpu.h>
 #include <linux/smp.h>
 #include <linux/idr.h>
 #include <linux/file.h>
 #include <linux/poll.h>
 #include <linux/slab.h>
 #include <linux/hash.h>
 #include <linux/tick.h>
 #include <linux/sysfs.h>
 #include <linux/dcache.h>
 #include <linux/percpu.h>
 #include <linux/ptrace.h>
 #include <linux/reboot.h>
 #include <linux/vmstat.h>
 #include <linux/device.h>
 #include <linux/export.h>
 #include <linux/vmalloc.h>
 #include <linux/hardirq.h>
 #include <linux/hugetlb.h>
 #include <linux/rculist.h>
 #include <linux/uaccess.h>
 #include <linux/syscalls.h>
 #include <linux/anon_inodes.h>
 #include <linux/kernel_stat.h>
 #include <linux/cgroup.h>
 #include <linux/perf_event.h>
 #include <linux/trace_events.h>
 #include <linux/hw_breakpoint.h>
 #include <linux/mm_types.h>
 #include <linux/module.h>
 #include <linux/mman.h>
 #include <linux/compat.h>
 #include <linux/bpf.h>
 #include <linux/filter.h>
 #include <linux/namei.h>
 #include <linux/parser.h>
 #include <linux/sched/clock.h>
 #include <linux/sched/mm.h>
 #include <linux/proc_ns.h>
 #include <linux/mount.h>
 #include <linux/min_heap.h>
 #include <linux/highmem.h>
 #include <linux/pgtable.h>
 #include <linux/buildid.h>
 
 #include "internal.h"
 
 
 #include <asm/irq_regs.h>
 
 // Tintin's code
 #include <linux/hrtimer.h>
 #include <linux/tintin_event.h>
 #include <linux/tintin_vector.h>
 #include "tintin_interface.h"
 #include "tintin_estimation.h"
 #include "tintin_uncertainty.h"
 #include "tintin_utils.h"
 #include "tintin_profiling_context.h"
 #include "core_tintin.h"
 
 
 #define PERF_FLAG_ALL                                                          \
 	(PERF_FLAG_FD_NO_GROUP | PERF_FLAG_FD_OUTPUT | PERF_FLAG_PID_CGROUP |  \
 	 PERF_FLAG_FD_CLOEXEC)
 
 #define TASK_TOMBSTONE ((void *)-1L)
 
 
 
 extern const struct file_operations perf_fops;
 
 static inline int tintin_perf_fget_light(int fd, struct fd *p)
 {
 	struct fd f = fdget(fd);
 	if (!f.file)
 		return -EBADF;
 
 	if (f.file->f_op != &perf_fops) {
 		fdput(f);
 		return -EBADF;
 	}
 	*p = f;
 	return 0;
 }
 
 static struct task_struct *find_lively_task_by_vpid(pid_t vpid)
 {
 	struct task_struct *task;
 
 	rcu_read_lock();
 	if (!vpid)
 		task = current;
 	else
 		task = find_task_by_vpid(vpid);
 	if (task)
 		get_task_struct(task);
 	rcu_read_unlock();
 
 	if (!task)
 		return ERR_PTR(-ESRCH);
 
 	return task;
 }
 
 static void perf_unpin_context(struct perf_event_context *ctx)
 {
 	unsigned long flags;
 
 	raw_spin_lock_irqsave(&ctx->lock, flags);
 	--ctx->pin_count;
 	raw_spin_unlock_irqrestore(&ctx->lock, flags);
 }
 
 static void free_task_ctx_data(struct pmu *pmu, void *task_ctx_data)
 {
 	if (pmu->task_ctx_cache && task_ctx_data)
 		kmem_cache_free(pmu->task_ctx_cache, task_ctx_data);
 }
 
 static void free_ctx(struct rcu_head *head)
 {
 	struct perf_event_context *ctx;
 
 	ctx = container_of(head, struct perf_event_context, rcu_head);
 	free_task_ctx_data(ctx->pmu, ctx->task_ctx_data);
 	kfree(ctx);
 }
 
 static void get_ctx(struct perf_event_context *ctx)
 {
 	refcount_inc(&ctx->refcount);
 }
 
 static void put_ctx(struct perf_event_context *ctx)
 {
 	if (refcount_dec_and_test(&ctx->refcount)) {
 		if (ctx->parent_ctx)
 			put_ctx(ctx->parent_ctx);
 		if (ctx->task && ctx->task != TASK_TOMBSTONE)
 			put_task_struct(ctx->task);
 		call_rcu(&ctx->rcu_head, free_ctx);
 	}
 }
 
 static bool perf_need_aux_event(struct perf_event *event)
 {
 	return !!event->attr.aux_output || !!event->attr.aux_sample_size;
 }
 
 static inline void perf_event__state_init(struct perf_event *event)
 {
 	event->state = event->attr.disabled ? PERF_EVENT_STATE_OFF :
 					      PERF_EVENT_STATE_INACTIVE;
 }
 
 // Tintin's code Newly added system call
/**
 * sys_tintin_event_open - open a performance event, associate it to a task/cpu
 *
 * @attr_uptr:	event_id type attributes for monitoring/sampling
 * @pid:		target pid
 * @cpu:		target cpu
 * @group_fd:		group leader event fd
 * @flags:		perf event open flags
 */
SYSCALL_DEFINE5(tintin_event_open, struct perf_event_attr __user *, attr_uptr,
		pid_t, pid, int, cpu, int, group_fd, unsigned long, flags)
{
	struct perf_event *group_leader = NULL, *output_event = NULL;
	struct perf_event *event, *sibling;
	struct perf_event_attr attr;
	struct perf_event_context *ctx, *gctx;
	struct file *event_file = NULL;
	struct fd group = { NULL, 0 };
	struct task_struct *task = NULL;
	struct pmu *pmu;
	int event_fd;
	int move_group = 0;
	int err;
	int f_flags = O_RDWR;
	int cgroup_fd = -1;

	struct tintin_event* sc;
	struct tintin_vector* tv;

	/* for future expandability... */
	if (flags & ~PERF_FLAG_ALL)
		return -EINVAL;

	/* Do we allow access to perf_event_open(2) ? */
	err = security_perf_event_open(&attr, PERF_SECURITY_OPEN);
	if (err)
		return err;

	err = perf_copy_attr(attr_uptr, &attr);
	if (err)
		return err;

	if (!attr.exclude_kernel) {
		err = perf_allow_kernel(&attr);
		if (err)
			return err;
	}

	if (attr.namespaces) {
		if (!perfmon_capable())
			return -EACCES;
	}

	if (attr.freq) {
		if (attr.sample_freq > sysctl_perf_event_sample_rate)
			return -EINVAL;
	} else {
		if (attr.sample_period & (1ULL << 63))
			return -EINVAL;
	}

	/* Only privileged users can get physical addresses */
	if ((attr.sample_type & PERF_SAMPLE_PHYS_ADDR)) {
		err = perf_allow_kernel(&attr);
		if (err)
			return err;
	}

	/* REGS_INTR can leak data, lockdown must prevent this */
	if (attr.sample_type & PERF_SAMPLE_REGS_INTR) {
		err = security_locked_down(LOCKDOWN_PERF);
		if (err)
			return err;
	}

	/*
	 * In cgroup mode, the pid argument is used to pass the fd
	 * opened to the cgroup directory in cgroupfs. The cpu argument
	 * designates the cpu on which to monitor threads from that
	 * cgroup.
	 */
	if ((flags & PERF_FLAG_PID_CGROUP) && (pid == -1 || cpu == -1))
		return -EINVAL;

	if (flags & PERF_FLAG_FD_CLOEXEC)
		f_flags |= O_CLOEXEC;

	event_fd = get_unused_fd_flags(f_flags);
	if (event_fd < 0)
		return event_fd;

	if (group_fd != -1) {
		// Tintin's code
		// err = perf_fget_light(group_fd, &group);
		err = tintin_perf_fget_light(group_fd, &group);
		// End of Tintin's code
		if (err)
			goto err_fd;
		group_leader = group.file->private_data;
		if (flags & PERF_FLAG_FD_OUTPUT)
			output_event = group_leader;
		if (flags & PERF_FLAG_FD_NO_GROUP)
			group_leader = NULL;
	}

	if (pid != -1 && !(flags & PERF_FLAG_PID_CGROUP)) {
		task = find_lively_task_by_vpid(pid);
		if (IS_ERR(task)) {
			err = PTR_ERR(task);
			goto err_group_fd;
		}
	}

	if (task && group_leader &&
	    group_leader->attr.inherit != attr.inherit) {
		err = -EINVAL;
		goto err_task;
	}

	if (flags & PERF_FLAG_PID_CGROUP)
		cgroup_fd = pid;

	event = perf_event_alloc(&attr, cpu, task, group_leader, NULL, NULL,
				 NULL, cgroup_fd);
	if (IS_ERR(event)) {
		err = PTR_ERR(event);
		goto err_task;
	}

	////// Modification, alloc and init tintin event
	/**
	* @Tintin's code in tintin_event_open
	*/


	sc = kzalloc(sizeof(struct tintin_event), GFP_KERNEL);
	tv  = kzalloc(sizeof(struct tintin_vector), GFP_KERNEL);

	tintin_vector_init(tv, 5000); // 5000 is a temporal magic number TODO
	sc->count_vec = tv;

	init_tintin_scheduling_context(sc);
	event->tintin_esx = sc;

	// Points bac to the event
	sc->_perf_event = event;

	event->event_tp = TINTIN_EVENT;
	///// End - Tintin's code

	if (is_sampling_event(event)) {
		if (event->pmu->capabilities & PERF_PMU_CAP_NO_INTERRUPT) {
			err = -EOPNOTSUPP;
			goto err_alloc;
		}
	}

	/*
	 * Special case software events and allow them to be part of
	 * any hardware group.
	 */
	pmu = event->pmu;

	if (attr.use_clockid) {
		err = perf_event_set_clock(event, attr.clockid);
		if (err)
			goto err_alloc;
	}

	if (pmu->task_ctx_nr == perf_sw_context)
		event->event_caps |= PERF_EV_CAP_SOFTWARE;

	if (group_leader) {
		if (is_software_event(event) &&
		    !in_software_context(group_leader)) {
			/*
			 * If the event is a sw event, but the group_leader
			 * is on hw context.
			 *
			 * Allow the addition of software events to hw
			 * groups, this is safe because software events
			 * never fail to schedule.
			 */
			pmu = group_leader->ctx->pmu;
		} else if (!is_software_event(event) &&
			   is_software_event(group_leader) &&
			   (group_leader->group_caps & PERF_EV_CAP_SOFTWARE)) {
			/*
			 * In case the group is a pure software group, and we
			 * try to add a hardware event, move the whole group to
			 * the hardware context.
			 */
			move_group = 1;
		}
	}

	/*
	 * Get the target context (task or percpu):
	 */
	ctx = find_get_context(pmu, task, event);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto err_alloc;
	}

	/*
	 * Look up the group leader (we will attach this event to it):
	 */
	if (group_leader) {
		err = -EINVAL;

		/*
		 * Do not allow a recursive hierarchy (this new sibling
		 * becoming part of another group-sibling):
		 */
		if (group_leader->group_leader != group_leader)
			goto err_context;

		/* All events in a group should have the same clock */
		if (group_leader->clock != event->clock)
			goto err_context;

		/*
		 * Make sure we're both events for the same CPU;
		 * grouping events for different CPUs is broken; since
		 * you can never concurrently schedule them anyhow.
		 */
		if (group_leader->cpu != event->cpu)
			goto err_context;

		/*
		 * Make sure we're both on the same task, or both
		 * per-CPU events.
		 */
		if (group_leader->ctx->task != ctx->task)
			goto err_context;

		/*
		 * Do not allow to attach to a group in a different task
		 * or CPU context. If we're moving SW events, we'll fix
		 * this up later, so allow that.
		 */
		if (!move_group && group_leader->ctx != ctx)
			goto err_context;

		/*
		 * Only a group leader can be exclusive or pinned
		 */
		if (attr.exclusive || attr.pinned)
			goto err_context;
	}

	if (output_event) {
		err = perf_event_set_output(event, output_event);
		if (err)
			goto err_context;
	}

	event_file =
		anon_inode_getfile("[perf_event]", &perf_fops, event, f_flags);
	if (IS_ERR(event_file)) {
		err = PTR_ERR(event_file);
		event_file = NULL;
		goto err_context;
	}

	if (task) {
		err = down_read_interruptible(&task->signal->exec_update_lock);
		if (err)
			goto err_file;

		/*
		 * We must hold exec_update_lock across this and any potential
		 * perf_install_in_context() call for this new event to
		 * serialize against exec() altering our credentials (and the
		 * perf_event_exit_task() that could imply).
		 */
		err = -EACCES;
		if (!perf_check_permission(&attr, task))
			goto err_cred;
	}

	if (move_group) {
		gctx = __perf_event_ctx_lock_double(group_leader, ctx);

		if (gctx->task == TASK_TOMBSTONE) {
			err = -ESRCH;
			goto err_locked;
		}

		/*
		 * Check if we raced against another sys_perf_event_open() call
		 * moving the software group underneath us.
		 */
		if (!(group_leader->group_caps & PERF_EV_CAP_SOFTWARE)) {
			/*
			 * If someone moved the group out from under us, check
			 * if this new event wound up on the same ctx, if so
			 * its the regular !move_group case, otherwise fail.
			 */
			if (gctx != ctx) {
				err = -EINVAL;
				goto err_locked;
			} else {
				perf_event_ctx_unlock(group_leader, gctx);
				move_group = 0;
			}
		}

		/*
		 * Failure to create exclusive events returns -EBUSY.
		 */
		err = -EBUSY;
		if (!exclusive_event_installable(group_leader, ctx))
			goto err_locked;

		for_each_sibling_event (sibling, group_leader) {
			if (!exclusive_event_installable(sibling, ctx))
				goto err_locked;
		}
	} else {
		mutex_lock(&ctx->mutex);
	}

	if (ctx->task == TASK_TOMBSTONE) {
		err = -ESRCH;
		goto err_locked;
	}

	if (!perf_event_validate_size(event)) {
		err = -E2BIG;
		goto err_locked;
	}

	if (!task) {
		/*
		 * Check if the @cpu we're creating an event for is online.
		 *
		 * We use the perf_cpu_context::ctx::mutex to serialize against
		 * the hotplug notifiers. See perf_event_{init,exit}_cpu().
		 */
		struct perf_cpu_context *cpuctx =
			container_of(ctx, struct perf_cpu_context, ctx);

		if (!cpuctx->online) {
			err = -ENODEV;
			goto err_locked;
		}
	}

	if (perf_need_aux_event(event) &&
	    !perf_get_aux_event(event, group_leader)) {
		err = -EINVAL;
		goto err_locked;
	}

	/*
	 * Must be under the same ctx::mutex as perf_install_in_context(),
	 * because we need to serialize with concurrent event creation.
	 */
	if (!exclusive_event_installable(event, ctx)) {
		err = -EBUSY;
		goto err_locked;
	}

	WARN_ON_ONCE(ctx->parent_ctx);

	/*
	 * This is the point on no return; we cannot fail hereafter. This is
	 * where we start modifying current state.
	 */

	if (move_group) {
		/*
		 * See perf_event_ctx_lock() for comments on the details
		 * of swizzling perf_event::ctx.
		 */
		perf_remove_from_context(group_leader, 0);
		put_ctx(gctx);

		for_each_sibling_event (sibling, group_leader) {
			perf_remove_from_context(sibling, 0);
			put_ctx(gctx);
		}

		/*
		 * Wait for everybody to stop referencing the events through
		 * the old lists, before installing it on new lists.
		 */
		synchronize_rcu();

		/*
		 * Install the group siblings before the group leader.
		 *
		 * Because a group leader will try and install the entire group
		 * (through the sibling list, which is still in-tact), we can
		 * end up with siblings installed in the wrong context.
		 *
		 * By installing siblings first we NO-OP because they're not
		 * reachable through the group lists.
		 */
		for_each_sibling_event (sibling, group_leader) {
			perf_event__state_init(sibling);
			perf_install_in_context(ctx, sibling, sibling->cpu);
			get_ctx(ctx);
		}

		/*
		 * Removing from the context ends up with disabled
		 * event. What we want here is event in the initial
		 * startup state, ready to be add into new context.
		 */
		perf_event__state_init(group_leader);
		perf_install_in_context(ctx, group_leader, group_leader->cpu);
		get_ctx(ctx);
	}

	/*
	 * Precalculate sample_data sizes; do while holding ctx::mutex such
	 * that we're serialized against further additions and before
	 * perf_install_in_context() which is the point the event is active and
	 * can use these values.
	 */
	perf_event__header_size(event);
	perf_event__id_header_size(event);

	event->owner = current;

	perf_install_in_context(ctx, event, event->cpu);
	perf_unpin_context(ctx);

	if (move_group)
		perf_event_ctx_unlock(group_leader, gctx);
	mutex_unlock(&ctx->mutex);

	if (task) {
		up_read(&task->signal->exec_update_lock);
		put_task_struct(task);
	}

	mutex_lock(&current->perf_event_mutex);
	list_add_tail(&event->owner_entry, &current->perf_event_list);
	mutex_unlock(&current->perf_event_mutex);

	/*
	 * Drop the reference on the group_event after placing the
	 * new event on the sibling_list. This ensures destruction
	 * of the group leader will find the pointer to itself in
	 * perf_group_detach().
	 */
	fdput(group);
	fd_install(event_fd, event_file);
	return event_fd;

err_locked:
	if (move_group)
		perf_event_ctx_unlock(group_leader, gctx);
	mutex_unlock(&ctx->mutex);
err_cred:
	if (task)
		up_read(&task->signal->exec_update_lock);
err_file:
	fput(event_file);
err_context:
	perf_unpin_context(ctx);
	put_ctx(ctx);
err_alloc:
	/*
	 * If event_file is set, the fput() above will have called ->release()
	 * and that will take care of freeing the event.
	 */
	if (!event_file)
		free_event(event);
err_task:
	if (task)
		put_task_struct(task);
err_group_fd:
	fdput(group);
err_fd:
	put_unused_fd(event_fd);
	return err;
}

/**
 * tintin_event_open_with_weight - open a performance event, associate it to a task/cpu
 *
 * @attr_uptr:	event_id type attributes for monitoring/sampling
 * @pid:		target pid
 * @cpu:		target cpu
 * @group_fd:		group leader event fd
 * @flags:		perf event open flags
 */
SYSCALL_DEFINE6(tintin_event_open_with_weight, struct perf_event_attr __user *, attr_uptr,
		pid_t, pid, int, cpu, int, group_fd, unsigned long, flags, unsigned int, weight)
{
	struct perf_event *group_leader = NULL, *output_event = NULL;
	struct perf_event *event, *sibling;
	struct perf_event_attr attr;
	struct perf_event_context *ctx, *gctx;
	struct file *event_file = NULL;
	struct fd group = { NULL, 0 };
	struct task_struct *task = NULL;
	struct pmu *pmu;
	int event_fd;
	int move_group = 0;
	int err;
	int f_flags = O_RDWR;
	int cgroup_fd = -1;
	struct tintin_event* sc;
	struct tintin_vector* tv;

	/* for future expandability... */
	if (flags & ~PERF_FLAG_ALL)
		return -EINVAL;

	/* Do we allow access to perf_event_open(2) ? */
	err = security_perf_event_open(&attr, PERF_SECURITY_OPEN);
	if (err)
		return err;

	err = perf_copy_attr(attr_uptr, &attr);
	if (err)
		return err;

	if (!attr.exclude_kernel) {
		err = perf_allow_kernel(&attr);
		if (err)
			return err;
	}

	if (attr.namespaces) {
		if (!perfmon_capable())
			return -EACCES;
	}

	if (attr.freq) {
		if (attr.sample_freq > sysctl_perf_event_sample_rate)
			return -EINVAL;
	} else {
		if (attr.sample_period & (1ULL << 63))
			return -EINVAL;
	}

	/* Only privileged users can get physical addresses */
	if ((attr.sample_type & PERF_SAMPLE_PHYS_ADDR)) {
		err = perf_allow_kernel(&attr);
		if (err)
			return err;
	}

	/* REGS_INTR can leak data, lockdown must prevent this */
	if (attr.sample_type & PERF_SAMPLE_REGS_INTR) {
		err = security_locked_down(LOCKDOWN_PERF);
		if (err)
			return err;
	}

	/*
	 * In cgroup mode, the pid argument is used to pass the fd
	 * opened to the cgroup directory in cgroupfs. The cpu argument
	 * designates the cpu on which to monitor threads from that
	 * cgroup.
	 */
	if ((flags & PERF_FLAG_PID_CGROUP) && (pid == -1 || cpu == -1))
		return -EINVAL;

	if (flags & PERF_FLAG_FD_CLOEXEC)
		f_flags |= O_CLOEXEC;

	event_fd = get_unused_fd_flags(f_flags);
	if (event_fd < 0)
		return event_fd;

	if (group_fd != -1) {
		// Tintin's code
		// err = perf_fget_light(group_fd, &group);
		err = tintin_perf_fget_light(group_fd, &group);
		// End of Tintin's code
		if (err)
			goto err_fd;
		group_leader = group.file->private_data;
		if (flags & PERF_FLAG_FD_OUTPUT)
			output_event = group_leader;
		if (flags & PERF_FLAG_FD_NO_GROUP)
			group_leader = NULL;
	}

	if (pid != -1 && !(flags & PERF_FLAG_PID_CGROUP)) {
		task = find_lively_task_by_vpid(pid);
		if (IS_ERR(task)) {
			err = PTR_ERR(task);
			goto err_group_fd;
		}
	}

	if (task && group_leader &&
	    group_leader->attr.inherit != attr.inherit) {
		err = -EINVAL;
		goto err_task;
	}

	if (flags & PERF_FLAG_PID_CGROUP)
		cgroup_fd = pid;

	event = perf_event_alloc(&attr, cpu, task, group_leader, NULL, NULL,
				 NULL, cgroup_fd);
	if (IS_ERR(event)) {
		err = PTR_ERR(event);
		goto err_task;
	}

	////// Modification, alloc and init tintin event
	/**
	* @Tintin's code in tintin_event_open
	*/


	sc = kzalloc(sizeof(struct tintin_event), GFP_KERNEL);
	tv  = kzalloc(sizeof(struct tintin_vector), GFP_KERNEL);

	tintin_vector_init(tv, 5000); // 5000 is a temporal magic number TODO
	sc->count_vec = tv;

	init_tintin_scheduling_context(sc);

	// Points bac to the event
	sc->_perf_event = event;

	// Assign the weight for scheduling
	
	// Ensure weight is within the range of 1 to 100
	weight = (weight > 100) ? 100 : ((weight == 0) ? 1 : weight);
	sc->weight = weight;

	event->tintin_esx = sc;
	event->event_tp = TINTIN_EVENT;

	///// End - Tintin's code

	if (is_sampling_event(event)) {
		if (event->pmu->capabilities & PERF_PMU_CAP_NO_INTERRUPT) {
			err = -EOPNOTSUPP;
			goto err_alloc;
		}
	}

	/*
	 * Special case software events and allow them to be part of
	 * any hardware group.
	 */
	pmu = event->pmu;

	if (attr.use_clockid) {
		err = perf_event_set_clock(event, attr.clockid);
		if (err)
			goto err_alloc;
	}

	if (pmu->task_ctx_nr == perf_sw_context)
		event->event_caps |= PERF_EV_CAP_SOFTWARE;

	if (group_leader) {
		if (is_software_event(event) &&
		    !in_software_context(group_leader)) {
			/*
			 * If the event is a sw event, but the group_leader
			 * is on hw context.
			 *
			 * Allow the addition of software events to hw
			 * groups, this is safe because software events
			 * never fail to schedule.
			 */
			pmu = group_leader->ctx->pmu;
		} else if (!is_software_event(event) &&
			   is_software_event(group_leader) &&
			   (group_leader->group_caps & PERF_EV_CAP_SOFTWARE)) {
			/*
			 * In case the group is a pure software group, and we
			 * try to add a hardware event, move the whole group to
			 * the hardware context.
			 */
			move_group = 1;
		}
	}

	/*
	 * Get the target context (task or percpu):
	 */
	ctx = find_get_context(pmu, task, event);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto err_alloc;
	}

	/*
	 * Look up the group leader (we will attach this event to it):
	 */
	if (group_leader) {
		err = -EINVAL;

		/*
		 * Do not allow a recursive hierarchy (this new sibling
		 * becoming part of another group-sibling):
		 */
		if (group_leader->group_leader != group_leader)
			goto err_context;

		/* All events in a group should have the same clock */
		if (group_leader->clock != event->clock)
			goto err_context;

		/*
		 * Make sure we're both events for the same CPU;
		 * grouping events for different CPUs is broken; since
		 * you can never concurrently schedule them anyhow.
		 */
		if (group_leader->cpu != event->cpu)
			goto err_context;

		/*
		 * Make sure we're both on the same task, or both
		 * per-CPU events.
		 */
		if (group_leader->ctx->task != ctx->task)
			goto err_context;

		/*
		 * Do not allow to attach to a group in a different task
		 * or CPU context. If we're moving SW events, we'll fix
		 * this up later, so allow that.
		 */
		if (!move_group && group_leader->ctx != ctx)
			goto err_context;

		/*
		 * Only a group leader can be exclusive or pinned
		 */
		if (attr.exclusive || attr.pinned)
			goto err_context;
	}

	if (output_event) {
		err = perf_event_set_output(event, output_event);
		if (err)
			goto err_context;
	}

	event_file =
		anon_inode_getfile("[perf_event]", &perf_fops, event, f_flags);
	if (IS_ERR(event_file)) {
		err = PTR_ERR(event_file);
		event_file = NULL;
		goto err_context;
	}

	if (task) {
		err = down_read_interruptible(&task->signal->exec_update_lock);
		if (err)
			goto err_file;

		/*
		 * We must hold exec_update_lock across this and any potential
		 * perf_install_in_context() call for this new event to
		 * serialize against exec() altering our credentials (and the
		 * perf_event_exit_task() that could imply).
		 */
		err = -EACCES;
		if (!perf_check_permission(&attr, task))
			goto err_cred;
	}

	if (move_group) {
		gctx = __perf_event_ctx_lock_double(group_leader, ctx);

		if (gctx->task == TASK_TOMBSTONE) {
			err = -ESRCH;
			goto err_locked;
		}

		/*
		 * Check if we raced against another sys_perf_event_open() call
		 * moving the software group underneath us.
		 */
		if (!(group_leader->group_caps & PERF_EV_CAP_SOFTWARE)) {
			/*
			 * If someone moved the group out from under us, check
			 * if this new event wound up on the same ctx, if so
			 * its the regular !move_group case, otherwise fail.
			 */
			if (gctx != ctx) {
				err = -EINVAL;
				goto err_locked;
			} else {
				perf_event_ctx_unlock(group_leader, gctx);
				move_group = 0;
			}
		}

		/*
		 * Failure to create exclusive events returns -EBUSY.
		 */
		err = -EBUSY;
		if (!exclusive_event_installable(group_leader, ctx))
			goto err_locked;

		for_each_sibling_event (sibling, group_leader) {
			if (!exclusive_event_installable(sibling, ctx))
				goto err_locked;
		}
	} else {
		mutex_lock(&ctx->mutex);
	}

	if (ctx->task == TASK_TOMBSTONE) {
		err = -ESRCH;
		goto err_locked;
	}

	if (!perf_event_validate_size(event)) {
		err = -E2BIG;
		goto err_locked;
	}

	if (!task) {
		/*
		 * Check if the @cpu we're creating an event for is online.
		 *
		 * We use the perf_cpu_context::ctx::mutex to serialize against
		 * the hotplug notifiers. See perf_event_{init,exit}_cpu().
		 */
		struct perf_cpu_context *cpuctx =
			container_of(ctx, struct perf_cpu_context, ctx);

		if (!cpuctx->online) {
			err = -ENODEV;
			goto err_locked;
		}
	}

	if (perf_need_aux_event(event) &&
	    !perf_get_aux_event(event, group_leader)) {
		err = -EINVAL;
		goto err_locked;
	}

	/*
	 * Must be under the same ctx::mutex as perf_install_in_context(),
	 * because we need to serialize with concurrent event creation.
	 */
	if (!exclusive_event_installable(event, ctx)) {
		err = -EBUSY;
		goto err_locked;
	}

	WARN_ON_ONCE(ctx->parent_ctx);

	/*
	 * This is the point on no return; we cannot fail hereafter. This is
	 * where we start modifying current state.
	 */

	if (move_group) {
		/*
		 * See perf_event_ctx_lock() for comments on the details
		 * of swizzling perf_event::ctx.
		 */
		perf_remove_from_context(group_leader, 0);
		put_ctx(gctx);

		for_each_sibling_event (sibling, group_leader) {
			perf_remove_from_context(sibling, 0);
			put_ctx(gctx);
		}

		/*
		 * Wait for everybody to stop referencing the events through
		 * the old lists, before installing it on new lists.
		 */
		synchronize_rcu();

		/*
		 * Install the group siblings before the group leader.
		 *
		 * Because a group leader will try and install the entire group
		 * (through the sibling list, which is still in-tact), we can
		 * end up with siblings installed in the wrong context.
		 *
		 * By installing siblings first we NO-OP because they're not
		 * reachable through the group lists.
		 */
		for_each_sibling_event (sibling, group_leader) {
			perf_event__state_init(sibling);
			perf_install_in_context(ctx, sibling, sibling->cpu);
			get_ctx(ctx);
		}

		/*
		 * Removing from the context ends up with disabled
		 * event. What we want here is event in the initial
		 * startup state, ready to be add into new context.
		 */
		perf_event__state_init(group_leader);
		perf_install_in_context(ctx, group_leader, group_leader->cpu);
		get_ctx(ctx);
	}

	/*
	 * Precalculate sample_data sizes; do while holding ctx::mutex such
	 * that we're serialized against further additions and before
	 * perf_install_in_context() which is the point the event is active and
	 * can use these values.
	 */
	perf_event__header_size(event);
	perf_event__id_header_size(event);

	event->owner = current;

	perf_install_in_context(ctx, event, event->cpu);
	perf_unpin_context(ctx);

	if (move_group)
		perf_event_ctx_unlock(group_leader, gctx);
	mutex_unlock(&ctx->mutex);

	if (task) {
		up_read(&task->signal->exec_update_lock);
		put_task_struct(task);
	}

	mutex_lock(&current->perf_event_mutex);
	list_add_tail(&event->owner_entry, &current->perf_event_list);
	mutex_unlock(&current->perf_event_mutex);

	/*
	 * Drop the reference on the group_event after placing the
	 * new event on the sibling_list. This ensures destruction
	 * of the group leader will find the pointer to itself in
	 * perf_group_detach().
	 */
	fdput(group);
	fd_install(event_fd, event_file);
	return event_fd;

err_locked:
	if (move_group)
		perf_event_ctx_unlock(group_leader, gctx);
	mutex_unlock(&ctx->mutex);
err_cred:
	if (task)
		up_read(&task->signal->exec_update_lock);
err_file:
	fput(event_file);
err_context:
	perf_unpin_context(ctx);
	put_ctx(ctx);
err_alloc:
	/*
	 * If event_file is set, the fput() above will have called ->release()
	 * and that will take care of freeing the event.
	 */
	if (!event_file)
		free_event(event);
err_task:
	if (task)
		put_task_struct(task);
err_group_fd:
	fdput(group);
err_fd:
	put_unused_fd(event_fd);
	return err;
}
// End - Syscall tintin_event_open_with_weight


// The pid parameter is optional
SYSCALL_DEFINE1(tintin_create_context, pid_t, pid)
{
	// Add the associations to events

	struct tintin_profiling_context* new_epx = kzalloc(sizeof(struct tintin_profiling_context), GFP_KERNEL);
	if (!new_epx)
		return -ENOMEM;

	tintin_init_profiling_context(new_epx);

	printk(KERN_INFO "[Tintin] Created new context with id %d\n", new_epx->id);
	return new_epx->id;
}

SYSCALL_DEFINE2(tintin_associate_context, unsigned, context_id1, unsigned, context_id2)
{

	// Create a new context that includes the two contexts to be associated

	printk(KERN_INFO "[Tintin] Associating context %d with context %d\n", context_id1, context_id2);

	return context_id1;
}

SYSCALL_DEFINE2(tintin_add_event, unsigned, context_id, unsigned, event_fd) {

	// Retrieve the tintin event

	// Then assign to the context

	printk(KERN_INFO "[Tintin] Adding event %d to context %d\n", event_fd, context_id);
	return 0;
}

// Difference in execution models: syscall vs file operations
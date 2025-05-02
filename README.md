# Tintin-kernel

This repository contains the source code of Tintin, based on Linux kernel version 5.15.

## Code structure

The main related files are listed as follows:

```
...
├── arch/
    ├── x86/
        ├── events/
            ├── core.c # Modified for Tintin on x86 CPU arch
├── include/
├── kernel/
    ...
    ├── trace/
    ├── sched/
    ├── events/
    │   ├── tintin_vector.c
    │   ├── tintin_utils.h
    │   ├── tintin_utils.c
    │   ├── tintin_uncertainty.h
    │   ├── tintin_uncertainty.c
    │   ├── tintin_sched_event_handler.c
    │   ├── tintin_profiling_context.h
    │   ├── tintin_profiling_context.c
    │   ├── tintin_interface.h
    │   ├── tintin_interface.c
    │   ├── tintin_hopcroft-karp.h
    │   ├── tintin_hopcroft-karp.c
    │   ├── tintin_estimation.h
    │   ├── tintin_estimation.c
    │   ├── tintin_elastic.h
    │   ├── tintin_elastic.c
    │   ├── core.c  # The main file of the original Linux perf subsystem

```

Tintin's code mostly located under the path `linux/kernel/events/tintin*.{c/h}`
Code added into linux's files are labeled by comments `Tintin's code`. Some functions in `kernel/events/core.c` have had the `static` keyword removed.

Example:
```C
if (event->owner) {
	// Tintin's code
	struct tintin_event* to_delete_sc = (struct tintin_event*) event->tintin_esx;
	tintin_remove_scheduling_context(to_delete_sc);
	// End of Tintin's code
			
	list_del_init(&event->owner_entry);
	smp_store_release(&event->owner, NULL);
	}
```


### Build and Install


```bash
$ sudo apt install build-essential libncurses-dev bison flex libssl-dev libelf-dev dwarves zstd
$ make -j
$ sudo make modules_install
# If the system boots with an "error: out of memory" error, use the following command
# $ sudo make INSTALL_MOD_STRIP=1 modules_install
$ sudo make install
$ sudo update-grub 
```


## Run Tintin

### Grant Non-root Access to PMU 

Check and modify `kernel.perf_event_paranoid`:

```
sudo sh -c "echo -1 > /proc/sys/kernel/perf_event_paranoid"
```

Or, add the above configuration to /etc/sysctl.conf to make it persistent

```
echo "kernel.perf_event_paranoid=-1" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

### Runtime re-configuartion

There are several parameters that can be tuned at runtime under the path `/proc/sys/tintin/`.

*** Disable NMI watchdog


You may disable the **NMI watchdog** to reduce **multiplexing** of performance monitoring events. The NMI watchdog is a Linux kernel feature that uses hardware performance monitoring unit (PMU) registers to detect system hangs. However, it consumes one of the PMU registers, which can interfere with accurate performance measurements.

By disabling the NMI watchdog, you free up the PMU register it uses, reducing multiplexing and improving the accuracy of performance monitoring.

To disable the NMI watchdog, you can use the following command:
```bash
sudo sysctl -w kernel.nmi_watchdog=0
```

To make this change permanent, add the following line to `/etc/sysctl.conf`:
```bash
kernel.nmi_watchdog=0
```

After making the change, verify that the NMI watchdog is disabled:
```bash
cat /proc/sys/kernel/nmi_watchdog
```
If the output is `0`, the NMI watchdog is successfully disabled.


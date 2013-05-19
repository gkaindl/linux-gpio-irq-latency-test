# Linux GPIO IRQ latency test

This is a simple kernel module to test the latency of GPIO interrupts on (embedded) Linux boards, such as the Raspberry Pi and the Beaglebone. It works by letting you connect two GPIO pins with a cable, then using one to generate interrupts, while the other pin is used to trigger them. It'll take the time between when the the interrupt is triggered and when the IRQ handler routine is being called. It will repeat this process a number of times and then print out the average response.

Some measurements that I took on are shown below.
                                             
## Instructions

- Choose your board by uncommenting the matching macro at the top of the this file. Currently, Raspberry Pi, Beaglebone and Beaglebone Black are supported.                                                  
- Compile the module with 'make'. Be sure that your build environment and kernel headers are set up.
- Connect a cable between the two pins used on your board:             
-- Raspberry Pi: Connect P1/16 and P1/18
-- Beaglebone (Black): 	Connect P9/12 and P9/15
- Issue 'insmod test-irq-latency.ko' to insert the module into the kernel.
- Watch the output in 'dmesg'.
- Once the module is finished, you can unload it again with 'rmmod test-irq-latency.ko'.
- That's it. Repeat 3-5 if you want to take another measurement.

Note that this is not an exact measurement, but it gives a pretty good overview of what latency you can expect in your kernel module. Obviously, IRQ latency from user space will be much higher. Also, you can get a good feel about how lowering your board's CPU frequency (like the 'ondemand' CPU governor does) affects IRQ latency.

Also note that on some (older) kernel versions, you might get very erratic and varying results. Unfortunately, this means you'll get a similar behavior in a real-world setting. After all, problems with erratic IRQ latencies in a kernel module I'm working on was the reason I made this test.

## Measurements

*Board*         *CPU speed*  *Kernel*        *Results*
Raspberry Pi        700mhz    3.6.11+         1024 passes. avg.  3770 nsecs
Beaglebone Black   1000mhz    3.8.12-bone17   1024 passes, avg.  4957 nsecs
Beaglebone          600mhz    3.2.21-psp15    1024 passes, avg.  7625 nsecs
Beaglebone          500mhz    3.2.21-psp15    1024 passes, avg.  8699 nsecs
Beaglebone Black    300mhz    3.8.12-bone17   1024 passes, avg. 14449 nsecs
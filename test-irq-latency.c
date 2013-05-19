/*******************************************************************************
 *                                                                             *
 * Linux GPIO IRQ latency test                                                 *
 *                                                                             *
 ******************************************************************************/

//#define BEAGLEBONE      // BB and BBB,   cable between p9/12 and p9/15
//#define RASPBERRY_PI    // Raspberry Pi, cable between p1/16 and p1/18

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/ioport.h>
#include <asm/io.h>

#define DRV_NAME           "test-irq-latency"
#define TEST_INTERVAL      (HZ/100)
#define NUM_TESTS          1024

#define MISSED_IRQ_MAX     (100)


struct irq_latency_test {
   struct timespec gpio_time, irq_time;
   u16 irq;
   u16 irq_pin, gpio_pin;
   u8 irq_fired, irq_enabled;
   struct timer_list timer;
   u32 test_count;
   unsigned long avg_nsecs, missed_irqs;
};

static struct irq_latency_test test_data;

static int setup_pinmux(void);


static irqreturn_t
test_irq_latency_interrupt_handler(int irq, void* dev_id)
{
   struct irq_latency_test* data = (struct irq_latency_test*)dev_id;
   
   getnstimeofday(&data->irq_time);
   data->irq_fired = 1;
   
   gpio_set_value(data->gpio_pin, 1);
   
   return IRQ_HANDLED;
}

static void
test_irq_latency_timer_handler(unsigned long ptr)
{
   struct irq_latency_test* data = (struct irq_latency_test*)ptr;
   u8 test_ok = 0;
   
   if (data->irq_fired) {
      struct timespec delta = timespec_sub(data->irq_time, data->gpio_time);

         if (delta.tv_sec > 0) {
            printk(KERN_INFO DRV_NAME
               " : GPIO IRQ triggered after > 1 sec, something is fishy.\n");
            data->missed_irqs++;
         } else {
            data->avg_nsecs = data->avg_nsecs ?
               (unsigned long)(((unsigned long long)delta.tv_nsec +
                  (unsigned long long)data->avg_nsecs) >> 1) :
                  delta.tv_nsec;
	   	   
            test_ok = 1;
         }

      data->irq_fired = 0;
   } else {
      data->missed_irqs++;
   }

   if (test_ok && ++data->test_count >= NUM_TESTS) {
      printk(KERN_INFO DRV_NAME
         " : finished %u passes. average GPIO IRQ latency is %lu nsecs.\n",
            NUM_TESTS, data->avg_nsecs);
	   		
      goto stopTesting;
   } else {
      if (data->missed_irqs > MISSED_IRQ_MAX) {
         printk(KERN_INFO DRV_NAME " : too many interrupts missed. "
            "check your jumper cable and reload this module!\n");

      goto stopTesting;
   }
	   
      mod_timer(&data->timer, jiffies + TEST_INTERVAL);
	   
      getnstimeofday(&data->gpio_time);
      gpio_set_value(data->gpio_pin, 0);
   }
	
   return;
	
stopTesting:
   if (test_data.irq_enabled) {
      disable_irq(test_data.irq);
      test_data.irq_enabled = 0;
   }
}

int __init
test_irq_latency_init_module(void)
{
   int err;

   err = setup_pinmux();
   if (err < 0) {
      printk(KERN_ALERT DRV_NAME " : failed to apply pinmux settings.\n");
      goto err_return;
   }
	
   err = gpio_request_one(test_data.gpio_pin, GPIOF_OUT_INIT_HIGH,
      DRV_NAME " gpio");
   if (err < 0) {
      printk(KERN_ALERT DRV_NAME " : failed to request GPIO pin %d.\n",
         test_data.gpio_pin);
      goto err_return;
   }
	
   err = gpio_request_one(test_data.irq_pin, GPIOF_IN, DRV_NAME " irq");
   if (err < 0) {
      printk(KERN_ALERT DRV_NAME " : failed to request IRQ pin %d.\n",
         test_data.irq_pin);
      goto err_free_gpio_return;
   }
	
   err = gpio_to_irq(test_data.irq_pin);
   if (err < 0) {
      printk(KERN_ALERT DRV_NAME " : failed to get IRQ for pin %d.\n",
         test_data.irq_pin);
      goto err_free_irq_return;
   } else {
      test_data.irq = (u16)err;
      err = 0;
   }
	
   err = request_any_context_irq(
      test_data.irq,
      test_irq_latency_interrupt_handler,
      IRQF_TRIGGER_FALLING | IRQF_DISABLED,
      DRV_NAME,
      (void*)&test_data
   );
   if (err < 0) {
      printk(KERN_ALERT DRV_NAME " : failed to enable IRQ %d for pin %d.\n",
         test_data.irq, test_data.irq_pin);
      goto err_free_irq_return;
   } else
      test_data.irq_enabled = 1;
	
   init_timer(&test_data.timer);
   test_data.timer.expires = jiffies + TEST_INTERVAL;
   test_data.timer.data = (unsigned long)&test_data;
   test_data.timer.function = test_irq_latency_timer_handler;
   add_timer(&test_data.timer);

   printk(KERN_INFO DRV_NAME
      " : beginning GPIO IRQ latency test (%u passes in %d seconds).\n",
      NUM_TESTS, (NUM_TESTS * TEST_INTERVAL) / HZ);
	
   return 0;

err_free_irq_return:
   gpio_free(test_data.irq_pin);
err_free_gpio_return:
   gpio_free(test_data.gpio_pin);
err_return:
   return err;
}

void __exit
test_irq_latency_exit_module(void)
{
   del_timer_sync(&test_data.timer);
	
   free_irq(test_data.irq, (void*)&test_data);
	
   gpio_free(test_data.irq_pin);
   gpio_free(test_data.gpio_pin);
	
   printk(KERN_INFO DRV_NAME " : unloaded IRQ latency test.\n");
}

module_init(test_irq_latency_init_module);
module_exit(test_irq_latency_exit_module);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("kernel module to test gpio irq latency.");
MODULE_VERSION("1.0");

#ifdef BEAGLEBONE
#define AM33XX_CONTROL_BASE		0x44e10000

static int
setup_pinmux(void)
{
   int i;
   static u32 pins[] = {
      AM33XX_CONTROL_BASE + 0x878,   // test pin (60): gpio1_28 (beaglebone p9/12)
      0x7 | (2 << 3),                //       mode 7 (gpio), PULLUP, OUTPUT
      AM33XX_CONTROL_BASE + 0x840,   // irq pin (48): gpio1_16 (beaglebone p9/15)
      0x7 | (2 << 3) | (1 << 5),     //       mode 7 (gpio), PULLUP, INPUT
   };

   for (i=0; i<4; i+=2) {
      void* addr = ioremap(pins[i], 4);

      if (NULL == addr)
         return -EBUSY;

      iowrite32(pins[i+1], addr);
      iounmap(addr);
   }

   test_data.irq_pin = 48;
   test_data.gpio_pin = 60;

   return 0;
}
#elif defined(RASPBERRY_PI)
static int
setup_pinmux(void)
{
// taken from
// https://github.com/bootc/linux/blob/rpi-i2cspi/drivers/spi/spi-bcm2708.c

#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= \
		(((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

   int pin_irq = 23, pin_gpio = 24;
   u32* gpio = ioremap(0x20200000, SZ_16K);
	
   if (NULL == gpio)
      return -EBUSY;

   // irq pin, GPIO 23 (pin P1/16)
   SET_GPIO_ALT(pin_irq, 0);
   INP_GPIO(pin_irq);
	
   // test pin, GPIO 24 (pin P1/18)
   SET_GPIO_ALT(pin_gpio, 0);
   INP_GPIO(pin_gpio);
   OUT_GPIO(pin_gpio);

   iounmap(gpio);

#undef INP_GPIO
#undef OUT_GPIO
#undef SET_GPIO_ALT

   test_data.irq_pin = pin_irq;
   test_data.gpio_pin = pin_gpio;

   return 0;
}
#else
#error PLEASE CHOOSE A VALID BOARD AT THE HEAD OF THIS FILE BEFORE COMPILING!
static int setup_pinmux(void) { return -ENODEV; }
#endif

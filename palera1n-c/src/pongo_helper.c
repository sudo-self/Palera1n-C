#include <errno.h>
#include <fcntl.h>              // open
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>             // exit, strtoull
#include <string.h>             // strlen, strerror, memcpy, memmove
#include <unistd.h>             // close
#include <sys/mman.h>           // mmap, munmap
#include <sys/stat.h>           // fstst

#include <palerain.h>
#include <ANSI-color-codes.h>

bool device_has_booted = 0;
int pongo_thr_running = 0;

#define ERR(...) LOG(LOG_VERBOSE, __VA_ARGS__)

void* pongo_helper(void* ptr) {
	pongo_thr_running = 1;
	pthread_cleanup_push(thr_cleanup, &pongo_thr_running);
	wait_for_pongo();
	while (get_spin()) {
		sleep(1);
	}
	pthread_cleanup_pop(1);
	return NULL;
}

void *pongo_usb_callback(void *arg) {
	if (get_found_pongo())
		return NULL;
	set_found_pongo(1);
	if (checkrain_option_enabled(palerain_flags, palerain_option_setup_rootful)) {
		strncat(xargs_cmd, " wdt=-1", 0x270 - strlen(xargs_cmd) - 1);	
	}
	LOG(LOG_INFO, "Found PongoOS USB Device");
	usb_device_handle_t handle = (((stuff_t *)arg)->handle);
	issue_pongo_command(handle, NULL);	
	issue_pongo_command(handle, "fuse lock");
	issue_pongo_command(handle, "sep auto");
	upload_pongo_file(handle, **kpf_to_upload, checkra1n_kpf_pongo_len);
	issue_pongo_command(handle, "modload");
	issue_pongo_command(handle, kpf_flags_cmd);
	issue_pongo_command(handle, checkrain_flags_cmd);
	issue_pongo_command(handle, palerain_flags_cmd);
	if (checkrain_option_enabled(palerain_flags, palerain_option_rootful))
	{
		issue_pongo_command(handle, "rootfs");
	}
#ifdef NO_RAMDISK
	if (ramdisk_dmg_len != 0)
#endif
	{
		strncat(xargs_cmd, " rootdev=md0", 0x270 - strlen(xargs_cmd) - 1);
		upload_pongo_file(handle, **ramdisk_to_upload, ramdisk_dmg_len);
		issue_pongo_command(handle, "ramdisk");
	}
#ifdef NO_BINPACK
	if (binpack_dmg_len != 0)
#endif
	{
		upload_pongo_file(handle, **overlay_to_upload, binpack_dmg_len);
		issue_pongo_command(handle, "overlay");
	}
	issue_pongo_command(handle, xargs_cmd);
	if (checkrain_option_enabled(host_flags, host_option_pongo_full)) goto done;
	issue_pongo_command(handle, "bootx");
	LOG(LOG_INFO, "Booting Kernel...");
	if (checkrain_option_enabled(palerain_flags, palerain_option_setup_partial_root)) {
		LOG(LOG_INFO, "Please wait up to 5 minutes for the bindfs to be created.");
		LOG(LOG_INFO, "Once the device boots up to iOS, run again without the -B (Create BindFS) option to jailbreak.");
	} else if (checkrain_option_enabled(palerain_flags, palerain_option_setup_rootful)) {
		LOG(LOG_INFO, "Please wait up to 10 minutes for the fakefs to be created.");
		LOG(LOG_INFO, "Once the device boots up to iOS, run again without the -c (Create FakeFS) option to jailbreak.");
	}
	if (dfuhelper_thr_running) {
		pthread_cancel(dfuhelper_thread);
		dfuhelper_thr_running = false;
	}
done:
	device_has_booted = true;
#ifdef USE_LIBUSB
	libusb_unref_device(((stuff_t *)arg)->dev);
#endif
	set_spin(0);
	return NULL;
}

int issue_pongo_command(usb_device_handle_t handle, char *command)
{
	uint32_t outpos = 0;
	uint32_t outlen = 0;
	int ret = USB_RET_SUCCESS;
	uint8_t in_progress = 1;
	if (command == NULL) goto fetch_output;
	size_t len = strlen(command);
	char command_buf[512];
	char stdout_buf[0x2000];
	if (len > (CMD_LEN_MAX - 2))
	{
		LOG(LOG_ERROR, "Pongo command %s too long (max %d)", command, CMD_LEN_MAX - 2);
		return EINVAL;
	}
    if (verbose < 3 || verbose > 4) {
	    LOG(LOG_VERBOSE, "Executing PongoOS command: '%s'", command);
    } else {
        printf("%s\n", command);
    }
	snprintf(command_buf, 512, "%s\n", command);
	len = strlen(command_buf);
	ret = USBControlTransfer(handle, 0x21, 4, 1, 0, 0, NULL, NULL);
	if (ret)
		goto bad;
	ret = USBControlTransfer(handle, 0x21, 3, 0, 0, (uint32_t)len, command_buf, NULL);
fetch_output:
	while (in_progress) {
		ret = USBControlTransfer(handle, 0xa1, 2, 0, 0, (uint32_t)sizeof(in_progress), &in_progress, NULL);
		if (ret == USB_RET_SUCCESS)
		{
			ret = USBControlTransfer(handle, 0xa1, 1, 0, 0, 0x1000, stdout_buf + outpos, &outlen);
			if (ret == USB_RET_SUCCESS)
			{
				write_stdout(stdout_buf + outpos, outlen);
				outpos += outlen;
				if (outpos > 0x1000)
				{
					memmove(stdout_buf, stdout_buf + outpos - 0x1000, 0x1000);
					outpos = 0x1000;
				}
			}
		}
		if (ret != USB_RET_SUCCESS)
		{
			goto bad;
		}
	}
bad:
	if (ret != USB_RET_SUCCESS)
	{
        if (command != NULL && (!strncmp("boot", command, 4))) {
			if (ret == USB_RET_IO || ret == USB_RET_NO_DEVICE || ret == USB_RET_NOT_RESPONDING)
				return 0;
		}
		LOG(LOG_ERROR, "USB error: %s", usb_strerror(ret));
		return ret;
	}
	else
		return ret;
}

int upload_pongo_file(usb_device_handle_t handle, unsigned char *buf, unsigned int buf_len)
{
	int ret = 0;
	ret = USBControlTransfer(handle, 0x21, 1, 0, 0, 4, &buf_len, NULL);
	if (ret == USB_RET_SUCCESS)
	{
		ret = USBBulkUpload(handle, buf, buf_len);
		if (ret == USB_RET_SUCCESS)
		{
		    if (verbose < 3 || verbose > 4) {
				LOG(LOG_VERBOSE, "Uploaded %llu bytes to PongoOS", (unsigned long long)buf_len);
    		} else {
        		printf("/send mem:%p:%p\n" BCYN "[Uploaded %llu bytes]\n" CRESET, (void*)buf, (void*)(buf + buf_len), (unsigned long long)buf_len);
    		}
		}
	}
	if (verbose >= 3) printf("pongoOS> ");
	return ret;
}

void io_start(stuff_t *stuff)
{
    int r = pthread_create(&stuff->th, NULL, &pongo_usb_callback, stuff);
    if(r != 0)
    {
        ERR("pthread_create: %s", strerror(r));
        set_spin(0);
		return;
    }
    pthread_join(stuff->th, NULL);
}

void io_stop(stuff_t *stuff)
{
    int r = pthread_cancel(stuff->th);
    if(r != 0)
    {
        ERR("pthread_cancel: %s", strerror(r));
        set_spin(0);
		return;
    }
    r = pthread_join(stuff->th, NULL);
    if(r != 0)
    {
        ERR("pthread_join: %s", strerror(r));
        set_spin(0);
		return;
    }
#ifdef USE_LIBUSB
	libusb_unref_device(stuff->dev);
#endif
}

void write_stdout(char *buf, uint32_t len)
{
    while(len > 0) {
        if (verbose >= 3) {
                ssize_t s = write(1, buf, len);
            if(s < 0) {
                LOG(LOG_ERROR, "write: %s", strerror(errno));
                pthread_exit(NULL);
            }
            buf += s;
            len -= s;
        } else break;
    }
}

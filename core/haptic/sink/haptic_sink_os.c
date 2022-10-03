/*
 Copyright (c) 2016 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include <stdlib.h>
#include <gimxinput/include/ginput.h>
#include <haptic/haptic_common.h>
#include <haptic/haptic_sink.h>

//#include <unistd.h>
#include <libudev.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

struct haptic_sink_state {
    int joystick;
};

static void g920_range_change(int range) {
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;
	struct udev_device *p_dev;

	/* Create the udev object */
	udev = udev_new();
	if (!udev) {
		printf("Can't create udev\n");
		exit(1);
	}
	
	/* Create a list of the devices in the 'input' subsystem, with ID_INPUT_JOYSTICK==1. */
	printf("Searching for Logitech G920 wheel...\n");
	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "hid");
	//udev_enumerate_add_match_property(enumerate, "ID_INPUT_JOYSTICK", "1");
	udev_enumerate_scan_devices(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);

	/* Identify the Logitech G920 wheel device (046d:c262). */
	udev_list_entry_foreach(dev_list_entry, devices) {
		const char *sys_path;
		
		sys_path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, sys_path);

		//printf("Sys Node Path: %s\n", sys_path);
		//printf("Device Node Path: %s\n", udev_device_get_devnode(dev));
		
        /* Check if we found a G920 wheel, then update range */
		p_dev = udev_device_get_parent_with_subsystem_devtype(
		       dev,
		       "usb",
		       "usb_device");
		
		/* Check that we actually found a parent device and that VID/PID match to a G920 */
		if ( p_dev && !strcmp(udev_device_get_sysattr_value(p_dev,"idVendor"), "046d") && !strcmp(udev_device_get_sysattr_value(p_dev, "idProduct"), "c262")) {
			printf("Logitech G920 identified with serial %s\n", udev_device_get_sysattr_value(p_dev, "serial"));
			printf("Current wheel range: %s\n", udev_device_get_sysattr_value(dev, "range"));

			/* Build the full sys_fs path to range element */
			char full_path[PATH_MAX];
			strcpy(full_path, sys_path);
			strcat(full_path, "/range");
			
			char char_range[BUFSIZ];
            snprintf(char_range, sizeof(char_range), "%hu", range);

            printf("Setting wheel range to: %s\n", char_range);

			/* Write new wheel range to range element */
			FILE *range_file;
			//printf("sys_fs path to range object: %s\n", full_path);
			range_file = fopen(full_path, "w");
			if (range_file != NULL) {
				fprintf(range_file, char_range);
				fclose(range_file);
			}
		}
		
		udev_device_unref(dev);
	}

	/* Free the enumerator object */
	udev_enumerate_unref(enumerate);
	udev_unref(udev);

}

static void dump_event(const GE_Event * event) {

    switch (event->type) {
    case GE_JOYRUMBLE:
        dprintf("< RUMBLE, weak=%hu, strong=%hu\n", event->jrumble.weak, event->jrumble.strong);
        break;
    case GE_JOYCONSTANTFORCE:
        dprintf("< CONSTANT, level: %d\n", event->jconstant.level);
        break;
    case GE_JOYSPRINGFORCE:
        dprintf("< SPRING, saturation: %u %u, coefficient: %u %u, center: %d, deadband: %u\n",
                event->jcondition.saturation.left, event->jcondition.saturation.right,
                event->jcondition.coefficient.left, event->jcondition.coefficient.right,
                event->jcondition.center, event->jcondition.deadband);
        break;
    case GE_JOYDAMPERFORCE:
        dprintf("< DAMPER, saturation: %u %u, coefficient: %d %d\n",
                event->jcondition.saturation.left, event->jcondition.saturation.right,
                event->jcondition.coefficient.left, event->jcondition.coefficient.right);
        break;
    default:
        dprintf("< UNKNOWN\n");
        break;
    }
}

static struct haptic_sink_state * haptic_sink_os_init(int joystick) {
    if (joystick >= 0) {
        int haptic = ginput_joystick_get_haptic(joystick);
        if (haptic & (GE_HAPTIC_RUMBLE | GE_HAPTIC_CONSTANT | GE_HAPTIC_SPRING | GE_HAPTIC_DAMPER)) {
            void * ptr = calloc(1, sizeof(struct haptic_sink_state));
            if (ptr != NULL) {
                struct haptic_sink_state * state = (struct haptic_sink_state *) ptr;
                state->joystick = joystick;
                return state;
            } else {
                PRINT_ERROR_ALLOC_FAILED("calloc");
            }
        }
    }
    return NULL;
}

static void haptic_sink_os_clean(struct haptic_sink_state * state) {

    free(state);
}

static void haptic_sink_os_process(struct haptic_sink_state * state, const s_haptic_core_data * data) {

    int ret = 0;

    GE_Event event = { .which = state->joystick };

    switch (data->type) {
    case E_DATA_TYPE_CONSTANT:
        event.type = GE_JOYCONSTANTFORCE;
        if (data->playing) {
            event.jconstant.level = data->constant.level;
        }
        ret = 1;
        break;
    case E_DATA_TYPE_SPRING:
        event.type = GE_JOYSPRINGFORCE;
        if (data->playing) {
            event.jcondition.saturation.left = data->spring.saturation.left;
            event.jcondition.saturation.right = data->spring.saturation.right;
            event.jcondition.coefficient.left = data->spring.coefficient.left;
            event.jcondition.coefficient.right = data->spring.coefficient.right;
            event.jcondition.center = data->spring.center;
            event.jcondition.deadband = data->spring.deadband;
        }
        ret = 1;
        break;
    case E_DATA_TYPE_DAMPER:
        event.type = GE_JOYDAMPERFORCE;
        if (data->playing) {
            event.jcondition.saturation.left = data->damper.saturation.left;
            event.jcondition.saturation.right = data->damper.saturation.right;
            event.jcondition.coefficient.left = data->damper.coefficient.left;
            event.jcondition.coefficient.right = data->damper.coefficient.right;
            event.jcondition.center = data->damper.center;
            event.jcondition.deadband = data->damper.deadband;
        }
        ret = 1;
        break;
    case E_DATA_TYPE_RANGE:

        if (!strcmp(ginput_joystick_name(state->joystick), "Logitech G920 Driving Force Racing Wheel")) {
            //attempt range change for G920 wheel
            g920_range_change(data->range.value);
            ginfo("wheel range adjusted to %hu degrees\n", data->range.value);
        } else {
            ginfo(_("adjust your wheel range to %u degrees\n"), data->range.value);
        }

        break;
    case E_DATA_TYPE_NONE:
    case E_DATA_TYPE_RUMBLE:
    case E_DATA_TYPE_LEDS:
        break;
    }

    if (ret != 0) {
        ginput_joystick_set_haptic(&event);
        if(gimx_params.debug.haptic) {
            dump_event(&event);
        }
    } else if (data->type == E_DATA_TYPE_RUMBLE) {
        GE_Event event = {
            .jrumble = {
                .type = GE_JOYRUMBLE,
                .which = state->joystick,
                .weak = data->rumble.weak,
                .strong = data->rumble.strong
            }
        };
        ginput_queue_push(&event);
        if(gimx_params.debug.haptic) {
            dump_event(&event);
        }
    }
}

static void haptic_sink_os_update(struct haptic_sink_state * state __attribute__((unused))) {

    // nothing to do here
}

static s_haptic_core_ids haptic_sink_os_ids[] = {
        /* This is a generic source, don't add anything here */
        { .vid = 0x0000,      .pid = 0x0000       }, // end of table
};

static s_haptic_sink sink_os = {
        .name = "haptic_sink_os",
        .ids = haptic_sink_os_ids,
        .caps = E_HAPTIC_SINK_CAP_RUMBLE | E_HAPTIC_SINK_CAP_CONSTANT | E_HAPTIC_SINK_CAP_SPRING | E_HAPTIC_SINK_CAP_DAMPER,
        .init = haptic_sink_os_init,
        .clean = haptic_sink_os_clean,
        .process = haptic_sink_os_process,
        .update = haptic_sink_os_update
};

void haptic_sink_os_constructor(void) __attribute__((constructor));
void haptic_sink_os_constructor(void) {

    haptic_sink_register(&sink_os);
}

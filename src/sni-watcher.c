#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>

#define WATCHER_PATH "/StatusNotifierWatcher"
#define KDE_INTERFACE "org.kde.StatusNotifierWatcher"
#define FDO_INTERFACE "org.freedesktop.StatusNotifierWatcher"
#define DEFAULT_ITEM_PATH "/StatusNotifierItem"

struct sni_item {
	char *id;
	char *service;
	char *sender;
	struct sni_item *next;
};

struct watcher_state {
	sd_bus *bus;
	struct sni_item *items;
};

static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int sig) {
	(void)sig;
	keep_running = 0;
}

static char *dup_string(const char *s) {
	size_t len = strlen(s) + 1;
	char *copy = malloc(len);
	if (copy != NULL) {
		memcpy(copy, s, len);
	}
	return copy;
}

static void free_item(struct sni_item *item) {
	if (item == NULL) {
		return;
	}
	free(item->id);
	free(item->service);
	free(item->sender);
	free(item);
}

static void emit_item_registered(sd_bus *bus, const char *id) {
	(void)sd_bus_emit_signal(bus, WATCHER_PATH, KDE_INTERFACE,
	                         "StatusNotifierItemRegistered", "s", id);
	(void)sd_bus_emit_signal(bus, WATCHER_PATH, FDO_INTERFACE,
	                         "StatusNotifierItemRegistered", "s", id);
	(void)sd_bus_emit_properties_changed(bus, WATCHER_PATH, KDE_INTERFACE,
	                                     "RegisteredStatusNotifierItems", NULL);
	(void)sd_bus_emit_properties_changed(bus, WATCHER_PATH, FDO_INTERFACE,
	                                     "RegisteredStatusNotifierItems", NULL);
}

static void emit_item_unregistered(sd_bus *bus, const char *id) {
	(void)sd_bus_emit_signal(bus, WATCHER_PATH, KDE_INTERFACE,
	                         "StatusNotifierItemUnregistered", "s", id);
	(void)sd_bus_emit_signal(bus, WATCHER_PATH, FDO_INTERFACE,
	                         "StatusNotifierItemUnregistered", "s", id);
	(void)sd_bus_emit_properties_changed(bus, WATCHER_PATH, KDE_INTERFACE,
	                                     "RegisteredStatusNotifierItems", NULL);
	(void)sd_bus_emit_properties_changed(bus, WATCHER_PATH, FDO_INTERFACE,
	                                     "RegisteredStatusNotifierItems", NULL);
}

static void emit_host_registered(sd_bus *bus) {
	(void)sd_bus_emit_signal(bus, WATCHER_PATH, KDE_INTERFACE,
	                         "StatusNotifierHostRegistered", "");
	(void)sd_bus_emit_signal(bus, WATCHER_PATH, FDO_INTERFACE,
	                         "StatusNotifierHostRegistered", "");
}

static int parse_item_registration(const char *arg, const char *sender,
                                   char **out_id, char **out_service) {
	const char *service_start = NULL;
	size_t service_len = 0;
	const char *path = NULL;

	if (arg == NULL || arg[0] == '\0' || sender == NULL || sender[0] == '\0') {
		return -EINVAL;
	}

	if (arg[0] == '/') {
		service_start = sender;
		service_len = strlen(sender);
		path = arg;
	} else {
		const char *slash = strchr(arg, '/');
		if (slash != NULL) {
			service_start = arg;
			service_len = (size_t)(slash - arg);
			path = slash;
		} else {
			service_start = arg;
			service_len = strlen(arg);
			path = DEFAULT_ITEM_PATH;
		}
	}

	if (service_len == 0 || path[0] != '/') {
		return -EINVAL;
	}

	size_t path_len = strlen(path);
	char *service = malloc(service_len + 1);
	char *id = malloc(service_len + path_len + 1);
	if (service == NULL || id == NULL) {
		free(service);
		free(id);
		return -ENOMEM;
	}

	memcpy(service, service_start, service_len);
	service[service_len] = '\0';

	memcpy(id, service_start, service_len);
	memcpy(id + service_len, path, path_len + 1);

	*out_id = id;
	*out_service = service;
	return 0;
}

static int method_register_item(sd_bus_message *m, void *userdata,
                                sd_bus_error *ret_error) {
	struct watcher_state *state = userdata;
	const char *arg = NULL;
	int r = sd_bus_message_read(m, "s", &arg);
	if (r < 0) {
		return r;
	}

	const char *sender = sd_bus_message_get_sender(m);
	char *id = NULL;
	char *service = NULL;
	r = parse_item_registration(arg, sender, &id, &service);
	if (r < 0) {
		return sd_bus_error_setf(ret_error, SD_BUS_ERROR_INVALID_ARGS,
		                         "Invalid StatusNotifierItem: %s",
		                         arg ? arg : "");
	}

	for (struct sni_item *cur = state->items; cur != NULL; cur = cur->next) {
		if (strcmp(cur->id, id) == 0) {
			char *new_sender = dup_string(sender);
			if (new_sender != NULL) {
				free(cur->sender);
				cur->sender = new_sender;
			}
			free(id);
			free(service);
			emit_item_registered(state->bus, cur->id);
			return sd_bus_reply_method_return(m, "");
		}
	}

	struct sni_item *item = calloc(1, sizeof(*item));
	char *sender_copy = dup_string(sender);
	if (item == NULL || sender_copy == NULL) {
		free(item);
		free(sender_copy);
		free(id);
		free(service);
		return -ENOMEM;
	}

	item->id = id;
	item->service = service;
	item->sender = sender_copy;
	item->next = state->items;
	state->items = item;

	emit_item_registered(state->bus, item->id);
	return sd_bus_reply_method_return(m, "");
}

static int method_register_host(sd_bus_message *m, void *userdata,
                                sd_bus_error *ret_error) {
	(void)ret_error;
	struct watcher_state *state = userdata;
	const char *service = NULL;
	int r = sd_bus_message_read(m, "s", &service);
	if (r < 0) {
		return r;
	}

	emit_host_registered(state->bus);
	return sd_bus_reply_method_return(m, "");
}

static int prop_get_registered_items(sd_bus *bus, const char *path,
                                     const char *interface,
                                     const char *property,
                                     sd_bus_message *reply, void *userdata,
                                     sd_bus_error *ret_error) {
	(void)bus;
	(void)path;
	(void)interface;
	(void)property;
	(void)ret_error;
	struct watcher_state *state = userdata;

	int r = sd_bus_message_open_container(reply, 'a', "s");
	if (r < 0) {
		return r;
	}

	for (struct sni_item *cur = state->items; cur != NULL; cur = cur->next) {
		r = sd_bus_message_append(reply, "s", cur->id);
		if (r < 0) {
			return r;
		}
	}

	return sd_bus_message_close_container(reply);
}

static int prop_get_host_registered(sd_bus *bus, const char *path,
                                    const char *interface,
                                    const char *property,
                                    sd_bus_message *reply, void *userdata,
                                    sd_bus_error *ret_error) {
	(void)bus;
	(void)path;
	(void)interface;
	(void)property;
	(void)userdata;
	(void)ret_error;
	return sd_bus_message_append(reply, "b", 1);
}

static int prop_get_protocol_version(sd_bus *bus, const char *path,
                                     const char *interface,
                                     const char *property,
                                     sd_bus_message *reply, void *userdata,
                                     sd_bus_error *ret_error) {
	(void)bus;
	(void)path;
	(void)interface;
	(void)property;
	(void)userdata;
	(void)ret_error;
	return sd_bus_message_append(reply, "i", 0);
}

static int on_name_owner_changed(sd_bus_message *m, void *userdata,
                                 sd_bus_error *ret_error) {
	(void)ret_error;
	struct watcher_state *state = userdata;
	const char *name = NULL;
	const char *old_owner = NULL;
	const char *new_owner = NULL;

	int r = sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner);
	if (r < 0 || name == NULL || new_owner == NULL || new_owner[0] != '\0') {
		return 0;
	}

	struct sni_item **link = &state->items;
	while (*link != NULL) {
		struct sni_item *cur = *link;
		if (strcmp(cur->service, name) == 0 ||
		    strcmp(cur->sender, name) == 0 ||
		    (old_owner != NULL && old_owner[0] != '\0' &&
		     strcmp(cur->sender, old_owner) == 0)) {
			*link = cur->next;
			emit_item_unregistered(state->bus, cur->id);
			free_item(cur);
		} else {
			link = &cur->next;
		}
	}

	return 0;
}

static const sd_bus_vtable watcher_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("RegisterStatusNotifierItem", "s", "",
	              method_register_item, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("RegisterStatusNotifierHost", "s", "",
	              method_register_host, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_PROPERTY("RegisteredStatusNotifierItems", "as",
	                prop_get_registered_items, 0,
	                SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("IsStatusNotifierHostRegistered", "b",
	                prop_get_host_registered, 0,
	                SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("ProtocolVersion", "i", prop_get_protocol_version, 0,
	                SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_SIGNAL("StatusNotifierItemRegistered", "s", 0),
	SD_BUS_SIGNAL("StatusNotifierItemUnregistered", "s", 0),
	SD_BUS_SIGNAL("StatusNotifierHostRegistered", "", 0),
	SD_BUS_SIGNAL("StatusNotifierHostUnregistered", "", 0),
	SD_BUS_VTABLE_END
};

static void cleanup_state(struct watcher_state *state) {
	while (state->items != NULL) {
		struct sni_item *next = state->items->next;
		free_item(state->items);
		state->items = next;
	}
	if (state->bus != NULL) {
		sd_bus_unref(state->bus);
		state->bus = NULL;
	}
}

int main(int argc, char **argv) {
	if (argc > 1) {
		if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
			printf("Usage: sni-watcher\n");
			return 0;
		}
		fprintf(stderr, "sni-watcher: unexpected arguments\n");
		return 1;
	}

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	struct watcher_state state = {0};
	int r = sd_bus_open_user(&state.bus);
	if (r < 0) {
		fprintf(stderr, "sni-watcher: failed to connect to user bus: %s\n",
		        strerror(-r));
		return 1;
	}

	r = sd_bus_add_object_vtable(state.bus, NULL, WATCHER_PATH,
	                             KDE_INTERFACE, watcher_vtable, &state);
	if (r < 0) {
		fprintf(stderr, "sni-watcher: failed to add KDE vtable: %s\n",
		        strerror(-r));
		cleanup_state(&state);
		return 1;
	}

	r = sd_bus_add_object_vtable(state.bus, NULL, WATCHER_PATH,
	                             FDO_INTERFACE, watcher_vtable, &state);
	if (r < 0) {
		fprintf(stderr, "sni-watcher: failed to add FDO vtable: %s\n",
		        strerror(-r));
		cleanup_state(&state);
		return 1;
	}

	r = sd_bus_match_signal(state.bus, NULL, "org.freedesktop.DBus",
	                        "/org/freedesktop/DBus",
	                        "org.freedesktop.DBus", "NameOwnerChanged",
	                        on_name_owner_changed, &state);
	if (r < 0) {
		fprintf(stderr, "sni-watcher: failed to subscribe to NameOwnerChanged: %s\n",
		        strerror(-r));
		cleanup_state(&state);
		return 1;
	}

	r = sd_bus_request_name(state.bus, KDE_INTERFACE, 0);
	if (r < 0) {
		fprintf(stderr, "sni-watcher: failed to acquire %s: %s\n",
		        KDE_INTERFACE, strerror(-r));
		cleanup_state(&state);
		return 1;
	}

	(void)sd_bus_request_name(state.bus, FDO_INTERFACE, 0);
	emit_host_registered(state.bus);

	while (keep_running) {
		r = sd_bus_process(state.bus, NULL);
		if (r < 0) {
			fprintf(stderr, "sni-watcher: bus error: %s\n", strerror(-r));
			cleanup_state(&state);
			return 1;
		}
		if (r > 0) {
			continue;
		}
		r = sd_bus_wait(state.bus, UINT64_MAX);
		if (r < 0 && r != -EINTR) {
			fprintf(stderr, "sni-watcher: wait error: %s\n", strerror(-r));
			cleanup_state(&state);
			return 1;
		}
	}

	cleanup_state(&state);
	return 0;
}

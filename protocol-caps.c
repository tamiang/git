#include "git-compat-util.h"
#include "protocol-caps.h"
#include "gettext.h"
#include "pkt-line.h"
#include "strvec.h"
#include "hash.h"
#include "object.h"
#include "object-store.h"
#include "string-list.h"
#include "strbuf.h"
#include "config.h"

struct requested_info {
	unsigned size : 1;
};

/*
 * Parses oids from the given line and collects them in the given
 * oid_str_list. Returns 1 if parsing was successful and 0 otherwise.
 */
static int parse_oid(const char *line, struct string_list *oid_str_list)
{
	const char *arg;

	if (!skip_prefix(line, "oid ", &arg))
		return 0;

	string_list_append(oid_str_list, arg);

	return 1;
}

/*
 * Validates and send requested info back to the client. Any errors detected
 * are returned as they are detected.
 */
static void send_info(struct repository *r, struct packet_writer *writer,
		      struct string_list *oid_str_list,
		      struct requested_info *info)
{
	struct string_list_item *item;
	struct strbuf send_buffer = STRBUF_INIT;

	if (!oid_str_list->nr)
		return;

	if (info->size)
		packet_writer_write(writer, "size");

	for_each_string_list_item (item, oid_str_list) {
		const char *oid_str = item->string;
		struct object_id oid;
		unsigned long object_size;

		if (get_oid_hex(oid_str, &oid) < 0) {
			packet_writer_error(
				writer,
				"object-info: protocol error, expected to get oid, not '%s'",
				oid_str);
			continue;
		}

		strbuf_addstr(&send_buffer, oid_str);

		if (info->size) {
			if (oid_object_info(r, &oid, &object_size) < 0) {
				strbuf_addstr(&send_buffer, " ");
			} else {
				strbuf_addf(&send_buffer, " %lu", object_size);
			}
		}

		packet_writer_write(writer, "%s", send_buffer.buf);
		strbuf_reset(&send_buffer);
	}
	strbuf_release(&send_buffer);
}

int cap_object_info(struct repository *r, struct packet_reader *request)
{
	struct requested_info info;
	struct packet_writer writer;
	struct string_list oid_str_list = STRING_LIST_INIT_DUP;

	packet_writer_init(&writer, 1);

	while (packet_reader_read(request) == PACKET_READ_NORMAL) {
		if (!strcmp("size", request->line)) {
			info.size = 1;
			continue;
		}

		if (parse_oid(request->line, &oid_str_list))
			continue;

		packet_writer_error(&writer,
				    "object-info: unexpected line: '%s'",
				    request->line);
	}

	if (request->status != PACKET_READ_FLUSH) {
		packet_writer_error(
			&writer, "object-info: expected flush after arguments");
		die(_("object-info: expected flush after arguments"));
	}

	send_info(r, &writer, &oid_str_list, &info);

	string_list_clear(&oid_str_list, 1);

	packet_flush(1);

	return 0;
}

static void send_lines(struct repository *r, struct packet_writer *writer,
		       struct string_list *str_list)
{
	struct string_list_item *item;

	if (!str_list->nr)
		return;

	for_each_string_list_item (item, str_list) {
		packet_writer_write(writer, "%s", item->string);
	}
}

int cap_config(struct repository *r, struct packet_reader *request)
{
	struct packet_writer writer;
	struct string_list config_str_list = STRING_LIST_INIT_DUP;
	int i = 0;
	const char *keys[] = {
		"bundle.fetchuri",
		"remote.self.promisor",
		"remote.self.partialclonefilter",
		"core.sparsecheckout",
		"core.sparsecheckoutcone",
		NULL
	};
	struct strbuf serve_config = STRBUF_INIT;
	struct strbuf key_equals_value = STRBUF_INIT;
	size_t len;
	strbuf_add(&serve_config, "serve.", 6);
	len = serve_config.len;

	packet_writer_init(&writer, 1);

	while (keys[i]) {
		struct string_list_item *item;
		const struct string_list *values = NULL;
		strbuf_setlen(&serve_config, len);
		strbuf_addstr(&serve_config, keys[i]);

		values = repo_config_get_value_multi(r, serve_config.buf);

		if (values) {
			for_each_string_list_item(item, values) {
				strbuf_reset(&key_equals_value);
				strbuf_addstr(&key_equals_value, keys[i]);
				strbuf_addch(&key_equals_value, '=');
				strbuf_addstr(&key_equals_value, item->string);

				string_list_append(&config_str_list, key_equals_value.buf);
			}
		}

		i++;
	}
	strbuf_release(&serve_config);
	strbuf_release(&key_equals_value);

	send_lines(r, &writer, &config_str_list);

	string_list_clear(&config_str_list, 1);

	packet_flush(1);

	return 0;
}

#include <server.h>
#include <stats.h>

void mark_pending_event_invalid(void *sptr)
{
	int i;
	for (i = 0; i < event_count; i++) {
		if (cur_events[i].data.ptr == sptr)
			cur_events[i].events = 0;
	}
}

int read_event_handler(server_info_t *server, int efd)
{
	struct sockaddr_in c_addr;
	int c_len, read_len;
	int accepted_fd;
	server_info_t *client_info;
	server->read_events++;
	switch (server->server_flags) {
		case LB_SERVER: 
		{
			c_len = sizeof(c_addr);
			if (0 > (accepted_fd = accept(server->fd, (struct sockaddr *)&c_addr, &c_len)))
				ASSERT(0);
			if (client_info = create_client_info(efd, accepted_fd)) {
				insert_client_info(client_info);
			}
			break;
		}
		case STATS_SERVER:
		{
			c_len = sizeof(c_addr);
			if (0 > (accepted_fd = accept(server->fd, (struct sockaddr *)&c_addr, &c_len)))
				ASSERT(0);
			if (client_info = create_client_info(efd, accepted_fd))
				client_info->server_flags |= STATS_CONN;
			break;
		}
		default:
		{
			if (!(server->server_flags & STATS_CONN) &&
			    NULL == server->session->server) {
				if (0 > attach_backend_lbserver(efd, server)) {
					close_client_pconn(server);
					lb_server->tot_unhandled_conn++;
					return -1;
				} else {
					lb_server->cur_conn++;
				}
			}
			ASSERT(server->session->buf_len <= BUF_LEN);
			if (BUF_LEN == server->session->buf_size)
				break;
			int start, end;
			if (BUF_LEN == server->session->buf_len)
				server->session->buf_len = 0;
			if (server->session->buf_len >= server->session->buf_read) {
				start = server->session->buf_len;
				end = BUF_LEN;
			} else {
				start = server->session->buf_len;
				end = server->session->buf_read;
			}
			if (0 > (read_len = read(server->fd, server->session->buf + start, end - start)))
				ASSERT(0);
			if (read_len > 0) {
				server->session->buf_len += read_len;
				server->session->buf_size += read_len;
			} else {
				ASSERT(!(server->server_flags & LB_SERVER));
				printf("closing connection data read = %d\n", server->session->buf_len);
				close_conn(efd, server);
				return 0;
			}
			if (server->server_flags & STATS_CONN)
				stats_read_req(server, efd);
		}
	}
	return 0;
}

int write_event_handler(server_info_t *server)
{
	server->write_events++;
	if (server->server_flags & STATS_CONN) {
		stats_server->write_events++;
		stats_write_res(server);
	} else {
		ASSERT(server->session->buf_read <= BUF_LEN);
		int start, end, write_len;
		server_info_t *rserver = server->session->server;
		if (BUF_LEN == rserver->session->buf_read &&
		    rserver->session->buf_size > 0)
			rserver->session->buf_read = 0;
		if (rserver->session->buf_len >= rserver->session->buf_read) {
			start = rserver->session->buf_read;
			end = rserver->session->buf_len;
		} else {
			start = rserver->session->buf_read;
			end = BUF_LEN;
		}
		if (start == end)
			return -1;

		if (0 > (write_len = write(server->fd, rserver->session->buf + start, end - start))) {
			perror("write");
			return -1;
		}
		rserver->session->buf_read += write_len;
		rserver->session->buf_size -= write_len;

		if (!(server->server_flags & BACKEND_SERVER) &&
			(rserver->session->buf_len == rserver->session->buf_read) &&
			(rserver->server_flags & CONN_CLOSED)) {
			close_client_conn(server);
		}
	}
}

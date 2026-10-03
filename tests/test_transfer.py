import http.server
import socketserver
import subprocess
import threading
import time
import unittest


class Transfer(unittest.TestCase):
    def request(self, url, cancel=200, timeout=5000):
        start = time.monotonic()
        result = subprocess.run(['./transfer_http', url, str(cancel), str(timeout)],
                                capture_output=True, timeout=3, check=True)
        self.assertLess(time.monotonic() - start, 2)
        return int(result.stdout)

    def test_cancel_and_timeout_on_silent_servers(self):
        for phase in ('headers', 'body', 'tls'):
            with self.subTest(phase=phase):
                stop = threading.Event()
                class Handler(socketserver.BaseRequestHandler):
                    def handle(self):
                        self.request.recv(4096)
                        if phase == 'body':
                            self.request.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 500\r\n\r\nx')
                        stop.wait(3)
                server = socketserver.ThreadingTCPServer(('127.0.0.1', 0), Handler)
                server.daemon_threads = True
                thread = threading.Thread(target=server.serve_forever, daemon=True)
                thread.start()
                url = f'{"https" if phase == "tls" else "http"}://127.0.0.1:{server.server_address[1]}/file'
                try:
                    self.assertEqual(self.request(url), 42)  # CURLE_ABORTED_BY_CALLBACK
                    self.assertEqual(self.request(url, -1, 300), 28)  # CURLE_OPERATION_TIMEDOUT
                finally:
                    stop.set()
                    server.shutdown()
                    server.server_close()
                    thread.join()

    def test_success_and_pre_cancel(self):
        requests = []
        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass
            def do_GET(self):
                requests.append(self.path)
                if self.path == '/redirect':
                    self.send_response(302)
                    self.send_header('Location', '/file')
                    self.end_headers()
                else:
                    self.send_response(200)
                    self.send_header('Content-Length', '2')
                    self.end_headers()
                    self.wfile.write(b'OK')
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        url = f'http://127.0.0.1:{server.server_port}'
        try:
            self.assertEqual(self.request(url + '/file', 0), 42)
            self.assertEqual(requests, [])
            self.assertEqual(self.request(url + '/redirect', -1), 0)
            self.assertEqual(requests, ['/redirect', '/file'])
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


    def test_requests_share_a_connection_until_closed(self):
        connections = []
        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.1'   # keep-alive
            def log_message(self, *args):
                pass
            def do_GET(self):
                self.send_response(200)
                self.send_header('Content-Length', '2')
                self.end_headers()
                self.wfile.write(b'OK')
        class Server(http.server.ThreadingHTTPServer):
            daemon_threads = True
            def process_request(self, request, address):
                connections.append(address)
                super().process_request(request, address)
        server = Server(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            result = subprocess.run(['./transfer_http', f'http://127.0.0.1:{server.server_port}/file', '-1', '5000', '3'],
                                    capture_output=True, timeout=5, check=True)
            self.assertEqual(result.stdout.split(), [b'0'] * 4)
            # Three requests over one connection; after closing it, a new one.
            self.assertEqual(len(connections), 2)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == '__main__':
    unittest.main()

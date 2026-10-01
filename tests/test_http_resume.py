import http.server
import os
import pathlib
import subprocess
import tempfile
import threading
import unittest

class ResumeHTTP(unittest.TestCase):
    def test_interrupted_and_changed_responses(self):
        for mode in ('range', 'ignore', 'changed', 'invalid', 'expired'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as root:
                requests = []
                class Handler(http.server.BaseHTTPRequestHandler):
                    def log_message(self, *args):
                        pass
                    def do_GET(self):
                        requests.append((self.headers.get('Range'), self.headers.get('If-Range')))
                        first = len(requests) == 1
                        ranged = self.headers.get('Range') is not None
                        if ranged and mode == 'expired':
                            self.send_response(416)
                            self.end_headers()
                            return
                        partial = ranged and mode != 'ignore'
                        self.send_response(206 if partial else 200)
                        self.send_header('ETag', '"new"' if ranged and mode == 'changed' else '"original"')
                        if partial:
                            self.send_header('Content-Range', 'bytes 2-5/6' if mode == 'invalid' else 'bytes 3-5/6')
                        self.send_header('Content-Length', '3' if partial else '6')
                        self.end_headers()
                        self.wfile.write(b'abc' if first else (b'def' if partial else b'abcdef'))
                        self.wfile.flush()
                        self.close_connection = True
                server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
                thread = threading.Thread(target=server.serve_forever, daemon=True)
                thread.start()
                try:
                    env = dict(os.environ, PM_FS_ROOT=root)
                    pathlib.Path(root, 'ms0').mkdir()
                    url = f'http://127.0.0.1:{server.server_port}/file'
                    def transfer():
                        return subprocess.run(['./resume_http', url, 'ms0:/payload'], env=env, capture_output=True)
                    first = transfer()
                    self.assertEqual(first.returncode, 1, first.stderr)
                    self.assertEqual(pathlib.Path(root, 'ms0/payload.part').read_bytes(), b'abc')
                    second = transfer()
                    self.assertEqual(second.returncode, 0, second.stderr)
                    self.assertEqual(requests[1], ('bytes=3-', '"original"'))
                    self.assertEqual(pathlib.Path(root, 'ms0/payload').read_bytes(), b'abcdef')
                    self.assertFalse(pathlib.Path(root, 'ms0/payload.part.json').exists())
                    if mode != 'range':
                        self.assertEqual(requests[2], (None, None))
                finally:
                    server.shutdown()
                    server.server_close()
                    thread.join()

if __name__ == '__main__':
    unittest.main()

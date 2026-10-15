"""Loopback-only synthetic transport boundary checks. No model or user text."""
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        self.rfile.read(int(self.headers.get('Content-Length', 0)))
        body = json.dumps({'choices': [{'finish_reason': 'stop', 'message': {
            'role': 'assistant', 'content': json.dumps({
                'version': 1, 'decision': 'no_change', 'suggestions': []})}}]}).encode()
        try:
            if self.path.startswith('/slow/'):
                time.sleep(5)
            if self.path.startswith('/redirect/'):
                self.send_response(302)
                self.send_header('Location', '/ok/chat/completions')
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            if self.path.startswith('/oversized/'):
                body = b'x' * 70000
            if self.path.startswith('/chunked/'):
                self.protocol_version = 'HTTP/1.1'
                self.send_response(200)
                self.send_header('Transfer-Encoding', 'chunked')
                self.end_headers()
                for offset in range(0, len(body), 7):
                    chunk = body[offset:offset + 7]
                    self.wfile.write(f'{len(chunk):x}\r\n'.encode() + chunk + b'\r\n')
                self.wfile.write(b'0\r\n\r\n')
                return
            if self.path.startswith('/stream-overflow/'):
                self.send_response(200)
                self.send_header('Connection', 'close')
                self.end_headers()
                self.wfile.write(b'x' * 70000)
                return
            self.send_response(200)
            if self.path.startswith('/truncated/'):
                self.send_header('Content-Length', str(len(body) + 100))
                self.send_header('Connection', 'close')
            else:
                self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            if self.path.startswith('/drip/'):
                for byte in body:
                    self.wfile.write(bytes([byte]))
                    self.wfile.flush()
                    time.sleep(.2)
            else:
                self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass


if __name__ == '__main__':
    import sys
    server = ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1])), Handler)
    print('ready', flush=True)
    server.serve_forever()

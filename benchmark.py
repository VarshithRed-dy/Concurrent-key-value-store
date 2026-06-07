import socket
import threading
import time

HOST = "127.0.0.1"
PORT = 6380

TOTAL_OPS = 100_000
THREADS = 10
OPS_PER_THREAD = TOTAL_OPS // THREADS


def worker(thread_id):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))

    for i in range(OPS_PER_THREAD):
        key = f"key:{thread_id}:{i}"
        value = f"value:{i}"

        # SET
        cmd = f"SET {key} {value}\n"
        s.sendall(cmd.encode())
        s.recv(1024)

        # GET
        cmd = f"GET {key}\n"
        s.sendall(cmd.encode())
        s.recv(1024)

    s.close()


def main():
    start = time.time()

    threads = []

    for i in range(THREADS):
        t = threading.Thread(target=worker, args=(i,))
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    end = time.time()

    elapsed = end - start
    actual_ops = THREADS * OPS_PER_THREAD * 2  # SET + GET

    print(f"Total operations: {actual_ops}")
    print(f"Threads: {THREADS}")
    print(f"Elapsed time: {elapsed:.2f} seconds")
    print(f"Throughput: {actual_ops / elapsed:.2f} ops/sec")


if __name__ == "__main__":
    main()
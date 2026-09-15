#!/usr/bin/env python3
"""Record a locally enabled tablet diagnostics session over Wi-Fi; never sends motion commands."""
import argparse
import datetime
import json
import time
import urllib.request


def snapshot(host, timeout=3):
    with urllib.request.urlopen(f'http://{host}:8080/diagnostics', timeout=timeout) as response:
        return json.load(response)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', help='IP shown on the tablet Diagnostics panel')
    parser.add_argument('--output', required=True, help='New JSON-lines log file')
    parser.add_argument('--seconds', type=float, default=300)
    parser.add_argument('--interval', type=float, default=1)
    args = parser.parse_args()
    if args.seconds <= 0 or args.interval < .25:
        parser.error('Use positive seconds and an interval of at least 0.25 seconds')
    end = time.monotonic() + args.seconds
    with open(args.output, 'x', encoding='utf-8') as output:
        try:
            while time.monotonic() < end:
                began = time.monotonic()
                record = {'received_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
                try:
                    record['diagnostics'] = value = snapshot(args.host)
                    print(f"{value['state']}: encoder {value['encoder_counts']}, RPM {value['rpm']:.2f}, "
                          f"fault {value['fault']}, age {value['sample_age_ms']} ms", flush=True)
                except (OSError, ValueError) as error:
                    record['error'] = str(error)
                    print(f'Diagnostics unavailable: {error}', flush=True)
                output.write(json.dumps(record) + '\n')
                output.flush()
                time.sleep(max(0, min(args.interval - (time.monotonic() - began), end - time.monotonic())))
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
import json
import sys
print(json.dumps({'version': 1, 'name': 'first-fixture', 'image': sys.argv[1],
                  'command': ['/usr/local/bin/runyard-fixture'], 'parameters': {'seed': 7, 'steps': 5, 'delay_ms': 100},
                  'resources': {'cpu_millis': 250, 'memory_mib': 128}}, indent=2))

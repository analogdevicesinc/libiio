#!/usr/bin/python

import os
import sys
import urllib2
import json

# This is pretty constant, but allow it to be overriden via env-var
url = os.getenv('TRAVIS_API_URL', 'https://api.travis-ci.org')

if (not url.lower().startswith("https://")):
    print (0)
    sys.exit(0)

ci_token = os.getenv('TRAVIS_API_TOKEN')
build_id = os.getenv('TRAVIS_BUILD_ID')

headers = {
    'Content-Type': 'application/json',
    'Accept': 'application/json',
    'Travis-API-Version': "3",
    'Authorization': "token {0}".format(ci_token)
}

# Codacy's bandit linter may complain that we haven't validated
# this URL for permitted schemes; we have validated this a few lines above
req = urllib2.Request("{0}/build/{1}/jobs".format(url, build_id),
		      headers=headers)

response = urllib2.urlopen(req).read()
json_r = json.loads(response.decode('utf-8'))

jobs_running = 0
for job in json_r['jobs']:
    # bump number of jobs higher, so nothing triggers
    if (job['state'] in [ 'canceled', 'failed' ]):
        jobs_running += 99
        break
    if (job['state'] in [ 'started', 'created', 'queued', 'received' ]):
        jobs_running += 1

print (jobs_running)

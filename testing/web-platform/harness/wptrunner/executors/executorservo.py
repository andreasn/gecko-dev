# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import hashlib
import json
import os
import subprocess
import tempfile
import threading
import urlparse
import uuid
from collections import defaultdict

from mozprocess import ProcessHandler

from .base import (ExecutorException,
                   Protocol,
                   RefTestImplementation,
                   testharness_result_converter,
                   reftest_result_converter)
from .process import ProcessTestExecutor


class ServoTestharnessExecutor(ProcessTestExecutor):
    convert_result = testharness_result_converter

    def __init__(self, browser, http_server_url, timeout_multiplier=1, debug_args=None,
                 pause_after_test=False):
        ProcessTestExecutor.__init__(self, browser, http_server_url,
                                     timeout_multiplier=timeout_multiplier,
                                     debug_args=debug_args)
        self.pause_after_test = pause_after_test
        self.result_data = None
        self.result_flag = None
        self.protocol = Protocol(self, browser, http_server_url)

    def do_test(self, test):
        self.result_data = None
        self.result_flag = threading.Event()

        self.command = [self.binary, "--cpu", "--hard-fail", "-z",
                        urlparse.urljoin(self.http_server_url, test.url)]

        if self.pause_after_test:
            self.command.remove("-z")

        if self.debug_args:
            self.command = list(self.debug_args) + self.command


        self.proc = ProcessHandler(self.command,
                                   processOutputLine=[self.on_output],
                                   onFinish=self.on_finish)
        self.proc.run()

        timeout = test.timeout * self.timeout_multiplier

        # Now wait to get the output we expect, or until we reach the timeout
        if self.debug_args is None and not self.pause_after_test:
            wait_timeout = timeout + 5
        else:
            wait_timeout = None
        self.result_flag.wait(wait_timeout)

        proc_is_running = True
        if self.result_flag.is_set() and self.result_data is not None:
            self.result_data["test"] = test.url
            result = self.convert_result(test, self.result_data)
        else:
            if self.proc.proc.poll() is not None:
                result = (test.result_cls("CRASH", None), [])
                proc_is_running = False
            else:
                result = (test.result_cls("TIMEOUT", None), [])

        if proc_is_running:
            if self.pause_after_test:
                self.logger.info("Pausing until the browser exits")
                self.proc.wait()
            else:
                self.proc.kill()

        return result

    def on_output(self, line):
        prefix = "ALERT: RESULT: "
        line = line.decode("utf8", "replace")
        if line.startswith(prefix):
            self.result_data = json.loads(line[len(prefix):])
            self.result_flag.set()
        else:
            if self.interactive:
                print line
            else:
                self.logger.process_output(self.proc.pid,
                                           line,
                                           " ".join(self.command))

    def on_finish(self):
        self.result_flag.set()


class TempFilename(object):
    def __init__(self, directory):
        self.directory = directory
        self.path = None

    def __enter__(self):
        self.path = os.path.join(self.directory, str(uuid.uuid4()))
        return self.path

    def __exit__(self, *args, **kwargs):
        try:
            os.unlink(self.path)
        except OSError:
            pass


class ServoRefTestExecutor(ProcessTestExecutor):
    convert_result = reftest_result_converter

    def __init__(self, browser, http_server_url, binary=None, timeout_multiplier=1,
                 screenshot_cache=None, debug_args=None, pause_after_test=False):
        ProcessTestExecutor.__init__(self,
                                     browser,
                                     http_server_url,
                                     timeout_multiplier=timeout_multiplier,
                                     debug_args=debug_args)

        self.protocol = Protocol(self, browser, http_server_url)
        self.screenshot_cache = screenshot_cache
        self.implementation = RefTestImplementation(self)
        self.tempdir = tempfile.mkdtemp()

    def teardown(self):
        os.rmdir(self.tempdir)
        ProcessTestExecutor.teardown(self)

    def screenshot(self, url, timeout):
        full_url = urlparse.urljoin(self.http_server_url, url)

        with TempFilename(self.tempdir) as output_path:
            self.command = [self.binary, "--cpu", "--hard-fail", "--exit",
                            "--output=%s" % output_path, full_url]

            self.proc = ProcessHandler(self.command,
                                       processOutputLine=[self.on_output])
            self.proc.run()
            rv = self.proc.wait(timeout=timeout)
            if rv is None:
                self.proc.kill()
                return False, ("EXTERNAL-TIMEOUT", None)

            if rv < 0:
                return False, ("CRASH", None)

            with open(output_path) as f:
                # Might need to strip variable headers or something here
                data = f.read()
                return True, data

    def do_test(self, test):
        result = self.implementation.run_test(test)

        return self.convert_result(test, result)

    def on_output(self, line):
        line = line.decode("utf8", "replace")
        if self.interactive:
            print line
        else:
            self.logger.process_output(self.proc.pid,
                                       line,
                                       " ".join(self.command))

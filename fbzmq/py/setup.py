#!/usr/bin/env python3

#
# Copyright (c) 2014-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

from __future__ import absolute_import, division, print_function

import os
from subprocess import check_call

from setuptools import find_packages, setup


THRIFT_FILES = ["service/if/Monitor.thrift"]


def create_package_list(base):
    """
    Get all packages under the base directory
    """

    return [base] + ["{}.{}".format(base, pkg) for pkg in find_packages(base)]


def generate_thrift_files():
    """
    Get list of all thrift files (absolute path names) and then generate
    python definitions for all thrift files.
    """

    current_dir = os.path.dirname(os.path.realpath(__file__))
    root_dir = os.path.dirname(current_dir)
    for thrift_file in THRIFT_FILES:
        print("> Generating python definition for {}".format(thrift_file))
        check_call(
            [
                "thrift1",
                "--gen",
                "py",
                "--out",
                current_dir,
                os.path.join(root_dir, thrift_file),
            ]
        )


generate_thrift_files()
setup(
    name="py-fbzmq",
    version="1.0",
    author="Open Routing",
    author_email="openr@fb.com",
    description="Python bindings for fbzmq thrift definitions",
    packages=create_package_list("fbzmq"),
    license="MIT",
)

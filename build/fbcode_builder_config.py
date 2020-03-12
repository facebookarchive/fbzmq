#!/usr/bin/env python

#
# Copyright (c) 2014-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

from __future__ import absolute_import, division, print_function, unicode_literals

import specs.fbthrift as fbthrift
import specs.fbzmq as fbzmq
import specs.fmt as fmt
import specs.folly as folly
import specs.gmock as gmock
from shell_quoting import ShellQuoted


"fbcode_builder steps to build & test Openr"


def fbcode_builder_spec(builder):
    return {
        "depends_on": [fmt, folly, fbthrift, gmock, fbzmq],
        "steps": [
            # fbzmq build dir will be the last workdir
            builder.step("Run fbzmq tests", [builder.run(ShellQuoted("make test"))])
        ],
    }


config = {
    "github_project": "facebook/fbzmq",
    "fbcode_builder_spec": fbcode_builder_spec,
}

#!/usr/bin/env python

#
# Copyright (c) 2014-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals
'fbcode_builder steps to build & test Openr'

import specs.fbthrift as fbthrift
import specs.folly as folly
import specs.fbzmq as fbzmq
import specs.gmock as gmock
import specs.sigar as sigar

from shell_quoting import ShellQuoted


def fbcode_builder_spec(builder):
    return {
        'depends_on': [folly, fbthrift, gmock, sigar, fbzmq],
        'steps': [
            # fbzmq build dir will be the last workdir
            builder.step('Run fbzmq tests', [
                builder.run(ShellQuoted('make test')),
            ]),
        ],
    }


config = {
    'github_project': 'facebook/fbzmq',
    'fbcode_builder_spec': fbcode_builder_spec,
}

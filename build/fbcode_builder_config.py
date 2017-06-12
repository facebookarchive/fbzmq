#!/usr/bin/env python
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals
'fbcode_builder steps to build & test Openr'

import specs.fbthrift as fbthrift
import specs.folly as folly
import specs.fbzmq as fbzmq
import specs.gmock as gmock

from shell_quoting import ShellQuoted


def fbcode_builder_spec(builder):
    return {
        'depends_on': [folly, fbthrift, gmock, fbzmq],
        'steps': [
            # fbzmq build dir will be the last workdir
            builder.step('Run fbzmq tests', [
                builder.run(ShellQuoted('make test')),
            ]),
        ],
    }


config = {
    'github_project': 'facebookincubator/fbzmq',
    'fbcode_builder_spec': fbcode_builder_spec,
}

#!/usr/bin/env python3

import re

class BaseFilter:

    def __init__(self):
        pass

    def __call__(self, config):
        return True


class LRUFilter(BaseFilter):

    def __init__(self):
        super().__init__()

    def __call__(self, config):
        return bool(re.match(r'lru', config['bin']))
#!/usr/bin/env python3


class Experiments:
    def __init__(self, result_sets=None):
        if result_sets is None:
            result_sets = list()
        self._sets = result_sets

    @property
    def sets(self):
        return self._sets

    def __iadd__(self, other):
        self._sets.append(other)
        return self

    def __truediv__(self, other):
        return Experiments([e for e in self._sets if all(item in e.config.items() for item in other.items())])

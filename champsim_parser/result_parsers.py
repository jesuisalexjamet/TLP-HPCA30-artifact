#!/usr/bin/env python3
import math
from abc import ABC, abstractmethod
import re
from pathlib import Path

from champsim_parser.filters.base_filter import BaseFilter
from champsim_parser.energy_modelling.cache_energy_model import CacheEnergyModel

from typing import Dict

import numpy as np

# Defining some regular expression constants to be able to parse the result file.
NUMERIC_CONST_PATTERN = r'[-+]? (?: (?: \d* \. \d+ ) | (?: \d+ \.? ) )(?: [Ee] [+-]? \d+ ) ?'
NUMERIC_CONST_PATTERN_RX = re.compile(NUMERIC_CONST_PATTERN, re.VERBOSE)

STLB_DOA_PATTERN = r'STLB irreg DOA (\d+) STLB irreg alive (\d+) STLB reg DOA (\d+) STLB reg alive (\d+)'
STLB_DOA_PATTERN_RX = re.compile(STLB_DOA_PATTERN)

STLB_LOAD_PATTERN = r'STLB LOAD\s+ACCESS: (\d+)\s+HIT: (\d+)\s+MISS: (\d+)'
STLB_LOAD_PATTERN_RX = re.compile(STLB_LOAD_PATTERN)

DOA_PATTERN = r'DOA: (\d+) non DOA: (\d+)'
DOA_PATTERN_RX = re.compile(DOA_PATTERN)

IRREG_ACCESS_PREDICTOR_ACCURACY_PATTERN = r'Stats on irregular data access predictions: Accurate \| (\d+) Inaccurate \| (\d+)'
IRREG_ACCESS_PREDICTOR_ACCURACY_PATTERN_RX = re.compile(
    IRREG_ACCESS_PREDICTOR_ACCURACY_PATTERN)

IRREG_ACCESS_PREDICTOR_TABLE_PATTERN = r'IRREG_PRED ACCESS: (\d+) HITS: (\d+) MISSES: (\d+) PREDICTION CHANGES: (\d+)'
IRREG_ACCESS_PREDICTOR_TABLE_PATTERN_RX = re.compile(
    IRREG_ACCESS_PREDICTOR_TABLE_PATTERN)

METADATA_CACHE_PATTERN = r'(\d+) (\d+) Miss ratio: (\d.\d+)'
METADATA_CACHE_PATTERN_RX = re.compile(METADATA_CACHE_PATTERN)

LOCMAP_PREDICTION_OUTCOME_PATTERN = r'(DRAM|L2C|LLC|L2C \+ LLC): (DRAM|L2C|LLC|L2C \+ LLC) (\d+)'
LOCMAP_PREDICTION_OUTCOME_PATTERN_RX = re.compile(
    LOCMAP_PREDICTION_OUTCOME_PATTERN)

MEMORY_ACCESS_PC_PATTERN = r'(\d+) different PCs threw memory accesses.'
MEMORY_ACCESS_PC_PATTERN_RX = re.compile(MEMORY_ACCESS_PC_PATTERN)

HERMES_PP_AGREEMENTS_PATTERN = r'HERMES\/PP AGREEMENTS: (\d+) (\d+)'
HERMES_PP_AGREEMENTS_PATTERN_RX = re.compile(HERMES_PP_AGREEMENTS_PATTERN)

HERMES_PP_L1D_SAVED_HERMES_REQUEST_PATTERN = r'Hermes request avoided upon L1D hits: (\d+)'
HERMES_PP_L2C_SAVED_HERMES_REQUEST_PATTERN = r'Hermes request avoided upon L2C hits: (\d+)'
HERMES_PP_LLC_SAVED_HERMES_REQUEST_PATTERN = r'Hermes request avoided upon LLC hits: (\d+)'
HERMES_PP_L1D_SAVED_HERMES_REQUEST_PATTERN_RX = re.compile(
    HERMES_PP_L1D_SAVED_HERMES_REQUEST_PATTERN)
HERMES_PP_L2C_SAVED_HERMES_REQUEST_PATTERN_RX = re.compile(
    HERMES_PP_L2C_SAVED_HERMES_REQUEST_PATTERN)
HERMES_PP_LLC_SAVED_HERMES_REQUEST_PATTERN_RX = re.compile(
    HERMES_PP_LLC_SAVED_HERMES_REQUEST_PATTERN)

DRAM_TRANSACTIONS = r'RQ ROW_BUFFER_HIT: (\d+)\s+ROW_BUFFER_MISS: (\d+)'
DRAM_TRANSACTIONS_RX = re.compile(DRAM_TRANSACTIONS)

PREFETCHERS_STATS = r'^pf\_requested: (?P<pf_requested>\d+) pf\_issued: (?P<pf_issued>\d+) pf\_useless: (?P<pf_useless>\d+) pf\_useful: (?P<pf_useful>\d+) pf\_fill: (?P<pf_fill>\d+)$'
PREFETCHERS_STATS_RX = re.compile(PREFETCHERS_STATS, re.MULTILINE)

L1D_PREFETCHERS_LOC_STATS = r'Useful prefetches L2C: (?P<pf_useful_l2c>\d+) LLC: (?P<pf_useful_llc>\d+) DRAM: (?P<pf_useful_dram>\d+)\nUseless prefetches L2C: (?P<pf_useless_l2c>\d+) LLC: (?P<pf_useless_llc>\d+) DRAM: (?P<pf_useless_dram>\d+)'
L1D_PREFETCHERS_LOC_STATS_RX = re.compile(L1D_PREFETCHERS_LOC_STATS, re.MULTILINE)

# Initialization state. --> We didn't encountered the beginning of the occurences.
REUSE_DISTANCE_OCCURENCES_STATE_0 = 0
# Start line has been encountered. Currently parsing.
REUSE_DISTANCE_OCCURENCES_STATE_1 = 1
# Parsing is done as we already encountered the last line of occurences.
REUSE_DISTANCE_OCCURENCES_STATE_2 = 2

DISTILL_CACHE_METRICS_STATE_0 = 0
DISTILL_CACHE_METRICS_STATE_1 = 1
DISTILL_CACHE_METRICS_STATE_2 = 2
DISTILL_CACHE_METRICS_STATE_3 = 3

SDC_CACHE_METRICS_STATE_0 = 0
SDC_CACHE_METRICS_STATE_1 = 1
SDC_CACHE_METRICS_STATE_2 = 2
SDC_CACHE_METRICS_STATE_3 = 3

SDC_RE_ACCURACY_STATE_0 = 0  # Init state.
SDC_RE_ACCURACY_STATE_1 = 1  # State after global accuracy.
SDC_RE_ACCURACY_STATE_2 = 2  # State after SDC->DRAM accuracy.
SDC_RE_ACCURACY_STATE_3 = 3  # State after SDC->L2C accuracy.
SDC_RE_ACCURACY_STATE_4 = 4  # State after SDC->LLC accuracy.

REQUESTS_STATE_0 = 0  # Initialization state on parsing requests
REQUESTS_STATE_1 = 1  # Requests from L1D
REQUESTS_STATE_2 = 2  # Requests from L2C
REQUESTS_STATE_3 = 3  # Request from LLC
REQUESTS_STATE_4 = 4  # Request from SDC
REQUESTS_STATE_5 = 5  # Request from DRAM
REQUESTS_STATE_6 = 6  # Parsing is done.

REQUESTED_SIZES_STATE_0 = 0  # Initialization state.
# Start line has been encountered. Currently parsing.
REQUESTED_SIZES_STATE_1 = 1
# Parsing is done as we already encountered the last line.
REQUESTED_SIZES_STATE_2 = 2

WOC_REUSE_STATE_0 = 0  # Initialization state.
WOC_REUSE_STATE_1 = 1  # Start line has been encountered. Currently parsing.
WOC_REUSE_STATE_2 = 2  # Parsing is done as already encountered the last line.

LOCMAP_OUTCOMES = ['L2C', 'LLC', 'L2C + LLC', 'DRAM']


class BaseResultParser(ABC):
    def __init__(self, filters):
        # A couple of checks on the given filters.
        if isinstance(filters, list):
            for e in filters:
                if not isinstance(e, BaseFilter):
                    raise Exception(f'{e} is not an instance of BaseFilter.')

            self._filters = filters
        else:
            if not isinstance(filters, BaseFilter):
                raise Exception(f'{filters} is not an instance of BaseFilter.')

            self._filters = [filters]

    @property
    def filters(self):
        return self._filters

    def apply_filters(self, struct_data, filters):
        # Stop case.
        if len(filters) == 1:
            return filters[0](struct_data)

        return filters[0](struct_data) and self.apply_filters(struct_data, filters[1:])

    @abstractmethod
    def __call__(self, data_fd):
        pass


def reuse_distance_occurrences_parser(data_fd):
    """
    This function, based on a provided file descriptor that points to a single result file generated by ChampSim,
    will extract the CPI and place into a structured format.

    :param data_fd:
    :return:
    """
    filename = Path(data_fd.name).name
    parser_state = REUSE_DISTANCE_OCCURENCES_STATE_0

    struct_data: Dict[str, any] = {
        'cpi': 0.0,

        'llc_misses': 0.0,
        'llc_mpki': 0.0,

        'accesses': 0,
        # Counts the number of blocks that only experienced a single access during the whole execution.
        'dead_blocks': 0,

        'reuse_distance_occurrences': {},
        'observed_reuse_distances': [],
        # Not initialized yet, but that will come later in this method.
        'np_observed_reuse_distances': None,

        'is_spec': False,
        'is_xsbench': False,
        'is_unionized': False,
        'is_qualcomm': False,
        'is_gapbs': False,
        'is_kron_urand': False,
    }

    # We seek for the benchmark suite this one belongs to.
    if re.match(r'^xs.[X]{1,2}L', filename):
        struct_data['is_xsbench'] = True
        struct_data['is_unionized'] = True if re.match(
            r'^xs.[X]{1,2}L\d+unionized', filename) else False
        # return  struct_data
    elif re.match(r'^(compute_fp|compute_int|srv|server)', filename):
        struct_data['is_qualcomm'] = True
        # return struct_data
    elif re.match(r'^(sssp|tc|bc|bfs|cc|pr)', filename):
        struct_data['is_gapbs'] = True
        struct_data['is_kron_urand'] = True if re.match(
            r'^.*(kron|urand)', filename) else False
    elif re.match(r'^\d{3}[.]{1}', filename):
        struct_data['is_spec'] = True
        # return struct_data

    for line in reversed(data_fd.readlines()):
        # If the parser FSM is in the final state, we stop the reading.
        if parser_state == REUSE_DISTANCE_OCCURENCES_STATE_2:
            break
        # If we are still in the initialization state and we encounter that sequence, we start recording.
        elif re.match("=== REUSE DISTANCES ===", line) and parser_state == REUSE_DISTANCE_OCCURENCES_STATE_0:
            parser_state = REUSE_DISTANCE_OCCURENCES_STATE_1
            continue
        # If we finally find a second occurence of the sequence, we stop recording.
        elif re.match("=== REUSE DISTANCES ===", line) and parser_state == REUSE_DISTANCE_OCCURENCES_STATE_1:
            parser_state = REUSE_DISTANCE_OCCURENCES_STATE_2
            continue
        elif parser_state == REUSE_DISTANCE_OCCURENCES_STATE_0:
            # If we haven't been able to detect anything qnd we remained in state 0, we go to the next iteration.
            continue

        # In other cases, we record reuse distance occurences.
        tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

        # struct_data['reuse_distance_occurrences'][int(tokens[0])] = int(tokens[1])
        struct_data['accesses'] += int(tokens[1])

        if int(tokens[0]) == 0:
            struct_data['dead_blocks'] += int(tokens[1])
            continue

        struct_data['observed_reuse_distances'] += max(
            int(int(tokens[1]) / 50), 1) * [int(tokens[0])]

    struct_data['np_observed_reuse_distances'] = np.asarray(
        struct_data['observed_reuse_distances'], dtype=int)

    del struct_data['observed_reuse_distances']

    return struct_data


def cpi_parser(data_fd):
    """
    This function, based on a provided file descriptor that points to a single result file generated by ChampSim,
    will extract the CPI and place into a structured format.

    :param data_fd:
    :return:
    """
    struct_data: Dict[str, float] = {
        'cpi': 0.0,
        'cycles': 0,

        'llc_misses': 0.0,
        'llc_mpki': 0.0,
    }

    for line in reversed(data_fd.readlines()):
        # The idea here is to find a line the provides the final IPC of the simulation.
        # Once we spotted that line, we compute the CPI and proceed.
        if re.match("CPU 0 cumulative IPC", line) is not None:
            # We trigger some actions such as extracting the IPC and then we break on the loop.
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            # Just checking some corner cases...
            if len(tokens) == 0:
                pass

            # Filling the structure with the appropriate data.
            try:
                struct_data['cpi'] = 1.0 / float(tokens[1])
            except Exception as err:
                continue
        elif re.match("LLC TOTAL", line) is not None:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            # Filling the structure with the appropriate data.
            if 'llc_misses' in struct_data:
                struct_data['llc_misses'] += float(tokens[-1])
            else:
                struct_data['llc_misses'] = float(tokens[-1])

        # elif re.match("LLC WRITEBACK", line) is not None:
        #     tokens = NUMERIC_CONST_PATTERN_RX.findall(line)
        #
        #     # Filling the structure with the appropriate data.
        #     if 'llc_misses' in struct_data:
        #         struct_data['llc_misses'] -= float(tokens[-1])
        #     else:
        #         struct_data['llc_misses'] = -float(tokens[-1])

    return True, struct_data


def sdc_cache_parser(data_fd):
    """
    This function, based on a provided file descriptor that points to a single result file generated by ChampSim,
    will extract the CPI and place into a structured format.

    :param data_fd:
    :return:
    """
    struct_data: Dict[str, float] = {
        'cpi': 0.0,

        'reg_accesses': 0.0,
        'reg_hits': 0.0,
        'reg_misses': 0.0,

        'irreg_accesses': 0.0,
        'irreg_hits': 0.0,
        'irreg_misses': 0.0,

        'llc_hits': 0.0,
        'llc_misses': 0.0,
        'llc_accesses': 0.0,

        'llc_mpki': 0.0,
        'reg_mpki': 0.0,
        'irreg_mpki': 0.0,
        'hits_pki': 0.0,
        'reg_hits_pki': 0.0,
        'irreg_hits_pki': 0.0,
    }

    for line in reversed(data_fd.readlines()):
        # The idea here is to find a line the provides the final IPC of the simulation.
        # Once we spotted that line, we compute the CPI and proceed.
        if re.match("CPU 0 cumulative IPC", line) is not None:
            # We trigger some actions such as extracting the IPC and then we break on the loop.
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            # Just checking some corner cases...
            if len(tokens) == 0:
                pass

            # Filling the structure with the appropriate data.
            struct_data['cpi'] = 1.0 / float(tokens[1])

    data_fd.seek(0, 0)

    parser_state = SDC_CACHE_METRICS_STATE_0

    for line in data_fd.readlines():
        if parser_state == SDC_CACHE_METRICS_STATE_2:
            break
        elif re.match(r"\[LLC metrics\]", line) and parser_state == SDC_CACHE_METRICS_STATE_0:
            parser_state = SDC_CACHE_METRICS_STATE_1
            continue
        elif re.match("TOTAL", line) and parser_state == SDC_CACHE_METRICS_STATE_1:
            parser_state = SDC_CACHE_METRICS_STATE_2

            # Parsing values.
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'llc_accesses': 0,
                'llc_hits': 1,
                'llc_misses': 2,

                'reg_hits': 3,
                'reg_misses': 4,

                'irreg_hits': 5,
                'irreg_misses': 6,
            }

            for k, v in data_mapping_dict.items():
                struct_data[k] = float(tokens[v])

        # elif re.match("LLC WRITEBACK", line) is not None:
        #     tokens = NUMERIC_CONST_PATTERN_RX.findall(line)
        #
        #     # Filling the structure with the appropriate data.
        #     if 'llc_misses' in struct_data:
        #         struct_data['llc_misses'] -= float(tokens[-1])
        #     else:
        #         struct_data['llc_misses'] = -float(tokens[-1])

    return True, struct_data


def distill_cache_parser(data_fd):
    parser_state = DISTILL_CACHE_METRICS_STATE_0
    valid, cpi_result_struct = cpi_parser(data_fd)

    data_fd.seek(0, 0)

    # if 'LOC_HIT' not in data_fd.read():
    #     return valid, cpi_result_struct

    # Updating the result structures with Distill Cache specific entries.
    cpi_result_struct.update({
        'sdc_accesses': 0.0,
        'sdc_hits': 0.0,
        'sdc_misses': 0.0,

        'sdc_avg_miss_latency': 0.0,

        'l1d_acceses': 0.0,
        'l1d_hits': 0.0,
        'l1d_reg_hits': 0.0,
        'l1d_irreg_hits': 0.0,
        'l1d_misses': 0.0,
        'l1d_reg_misses': 0.0,
        'l1d_irreg_misses': 0.0,

        'l1d_bypass': 0.0,
        'l1d_no_bypass': 0.0,
        'l1d_bypass_predictor': {
            'accurate': 0.0,
            'inaccurate': 0.0,
        },

        'l1d_avg_miss_latency': 0.0,

        'l2c_accesses': 0.0,
        'l2c_hits': 0.0,
        'l2c_reg_hits': 0.0,
        'l2c_irreg_hits': 0.0,
        'l2c_misses': 0.0,
        'l2c_reg_misses': 0.0,
        'l2c_irreg_misses': 0.0,
        'l2c_pf_requested': 0.0,
        'l2c_pf_useful': 0.0,
        'l2c_pf_useless': 0.0,

        'l2c_avg_miss_latency': 0.0,

        'llc_accesses': 0.0,
        'llc_loc_hit': 0.0,
        'llc_woc_hit': 0.0,
        'llc_hole_miss': 0.0,
        'llc_line_miss': 0.0,

        'irreg_hits': 0.0,
        'irreg_hits_pki': 0.0,
        'irreg_misses': 0.0,
        'irreg_mpki': 0.0,
        'reg_hits': 0.0,
        'reg_hits_pki': 0.0,
        'reg_misses': 0.0,
        'reg_mpki': 0.0,

        'requested_sizes': np.zeros(shape=8, dtype=int),

        'served_from': {
            'SDC': 0,
            'L1D': 0,
            'L2C': 0,
            'LLC': 0,
            'DRAM': 0,
        },

        'routing_engine': {
            'accuracy': {
                'global': 0.0,
                'l2c': 0.0,
                'llc': 0.0,
                'dram': 0.0,
            }
        },

        'stlb': {
            'irreg_doa': 0,
            'irreg_alive': 0,
            'reg_doa': 0,
            'reg_alive': 0,

            'accesses': 0,
            'misses': 0,

            'doa': 0,
            'non_doa': 0
        },

        'irregular_accesses': {
            'accurate': 0,
            'inaccurate': 0,
        },

        'irregular_predictor': {
            'hits': 0,
            'misses': 0,
            'accesses': 0,
            'changes': 0,
        },

        'metadata_cache': {
            'misses': 0,
            'miss_rate': 0.0,
        },

        'locmap': {

        },

        'offchip_pred': {
            'true_pos': 0,
            'false_pos': 0,
            'false_neg': 0,
            'true_neg': 0,

            'miss_hit_l1d': 0,
            'miss_hit_l2c': 0,

            'true_pos_pf': 0,
            'false_pos_pf': 0,
            'false_neg_pf': 0,
            'true_neg_pf': 0,
        },

        'l1d_offchip_pred': {
            'true_pos': 0,
            'false_pos': 0,
            'false_neg': 0,
            'true_neg': 0,

            'l1d_saved_hermes_requests': 0,
            'l2c_saved_hermes_requests': 0,
            'llc_saved_hermes_requests': 0,
        },

        'offchip_pred_hybrid': {
            'aggreements_check': 0,
            'aggreements': 0,
        },

        'dram': {
            'transactions': 0,
            'congested_events': 0,
        },

        'woc_reuses': {},

        'is_llc_distill_cache': False,

        'l1d_prefetcher': {
            'pf_fill': 0,
            'pf_useful': 0,

            'useful': {
                'l2c': 0,
                'llc': 0,
                'dram': 0,
            },

            'useless': {
                'l2c': 0,
                'llc': 0,
                'dram': 0,
            }
        },

        'l2c_prefetcher': {
            'pf_fill': 0,
            'pf_useful': 0
        },

        'llc_prefetcher': {
            'pf_fill': 0,
            'pf_useful': 0
        },
    })

    for i in LOCMAP_OUTCOMES:
        cpi_result_struct['locmap'].update({
            i: {}
        })

        for j in LOCMAP_OUTCOMES:
            cpi_result_struct['locmap'][i].update({
                j: 0
            })

    # Going back at the beginning of the file.
    data_fd.seek(0, 0)

    for line in data_fd.readlines():
        if parser_state == DISTILL_CACHE_METRICS_STATE_2:
            break
        elif re.match(r"\[LLC metrics\]", line) and parser_state == DISTILL_CACHE_METRICS_STATE_0:
            parser_state = REUSE_DISTANCE_OCCURENCES_STATE_1
            continue
        elif re.match("LOAD", line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            # Parsing values.
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            cpi_result_struct['llc_loads'] = float(tokens[0])
        elif re.match("RFO", line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            # Parsing values.
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            cpi_result_struct['llc_stores'] = float(tokens[0])
        elif re.match("PREFETCH", line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            # Parsing values.
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            cpi_result_struct['llc_loads'] += float(tokens[0])

            # In this situation, the field are left unchanged.
            if len(tokens) == 3:
                cpi_result_struct['llc_accesses'] = -float(tokens[0])
                cpi_result_struct['llc_loc_hit'] = -float(tokens[1])
                cpi_result_struct['llc_woc_hit'] = 0
                cpi_result_struct['llc_line_miss'] = -float(tokens[2])
                cpi_result_struct['llc_hole_miss'] = 0
            elif len(tokens) == 5:
                cpi_result_struct['llc_accesseses'] = -float(tokens[0])
                cpi_result_struct['llc_loc_hit'] = -float(tokens[1])
                cpi_result_struct['llc_woc_hit'] = -float(tokens[2])
                cpi_result_struct['llc_hole_miss'] = -float(tokens[3])
                cpi_result_struct['llc_line_miss'] = -float(tokens[4])
            elif len(tokens) == 7:
                cpi_result_struct['llc_accesses'] = -float(tokens[0])
                cpi_result_struct['llc_loc_hit'] = -float(tokens[1])
                cpi_result_struct['llc_line_miss'] = -float(tokens[2])
                cpi_result_struct['reg_hits'] = -float(tokens[3])
                cpi_result_struct['reg_misses'] = -float(tokens[4])
                cpi_result_struct['irreg_hits'] = -float(tokens[5])
                cpi_result_struct['irreg_misses'] = -float(tokens[6])
        elif re.match("WRITEBACK", line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            # Parsing values.
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            # In this situation, the field are left unchanged.
            if len(tokens) == 3:
                cpi_result_struct['llc_accesses'] -= float(tokens[0])
                cpi_result_struct['llc_loc_hit'] -= float(tokens[1])
                cpi_result_struct['llc_woc_hit'] -= 0
                cpi_result_struct['llc_line_miss'] -= float(tokens[2])
                cpi_result_struct['llc_hole_miss'] -= 0
            elif len(tokens) == 5:
                cpi_result_struct['llc_accesseses'] -= float(tokens[0])
                cpi_result_struct['llc_loc_hit'] -= float(tokens[1])
                cpi_result_struct['llc_woc_hit'] -= float(tokens[2])
                cpi_result_struct['llc_hole_miss'] -= float(tokens[3])
                cpi_result_struct['llc_line_miss'] -= float(tokens[4])
            elif len(tokens) == 7:
                cpi_result_struct['llc_accesses'] -= float(tokens[0])
                cpi_result_struct['llc_loc_hit'] -= float(tokens[1])
                cpi_result_struct['llc_line_miss'] -= float(tokens[2])
                cpi_result_struct['reg_hits'] -= float(tokens[3])
                cpi_result_struct['reg_misses'] -= float(tokens[4])
                cpi_result_struct['irreg_hits'] -= float(tokens[5])
                cpi_result_struct['irreg_misses'] -= float(tokens[6])
        elif re.match("TOTAL", line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            parser_state = DISTILL_CACHE_METRICS_STATE_2

            # Parsing values.
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            # In this situation, the field are left unchanged.
            if len(tokens) == 3:
                cpi_result_struct['llc_accesses'] += float(tokens[0])
                cpi_result_struct['llc_loc_hit'] += float(tokens[1])
                cpi_result_struct['llc_woc_hit'] += 0
                cpi_result_struct['llc_line_miss'] += float(tokens[2])
                cpi_result_struct['llc_hole_miss'] += 0
            elif len(tokens) == 5:
                cpi_result_struct['llc_accesseses'] += float(tokens[0])
                cpi_result_struct['llc_loc_hit'] += float(tokens[1])
                cpi_result_struct['llc_woc_hit'] += float(tokens[2])
                cpi_result_struct['llc_hole_miss'] += float(tokens[3])
                cpi_result_struct['llc_line_miss'] += float(tokens[4])
            elif len(tokens) == 7:
                cpi_result_struct['llc_accesses'] += float(tokens[0])
                cpi_result_struct['llc_loc_hit'] += float(tokens[1])
                cpi_result_struct['llc_line_miss'] += float(tokens[2])
                cpi_result_struct['reg_hits'] += float(tokens[3])
                cpi_result_struct['reg_misses'] += float(tokens[4])
                cpi_result_struct['irreg_hits'] += float(tokens[5])
                cpi_result_struct['irreg_misses'] += float(tokens[6])

            continue

    # Going back at the beginning of the file. Reading L1D stats.
    data_fd.seek(0, 0)

    parser_state = DISTILL_CACHE_METRICS_STATE_0

    for line in data_fd.readlines():
        if parser_state == DISTILL_CACHE_METRICS_STATE_3:
            break
        elif re.match(r'\[L1D metrics\]', line) and parser_state == DISTILL_CACHE_METRICS_STATE_0:
            parser_state = DISTILL_CACHE_METRICS_STATE_1
            continue
        elif re.match('LOAD', line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'l1d_loads': 0,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] = float(tokens[v])
        elif re.match('RFO', line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'l1d_stores': 0,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] = float(tokens[v])
        elif re.match('PREFETCH', line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'l1d_accesses': 0,
                'l1d_hits': 1,
                'l1d_misses': 2,
                'l1d_reg_hits': 3,
                'l1d_reg_misses': 4,
                'l1d_irreg_hits': 5,
                'l1d_irreg_misses': 6,
            }

            if len(tokens) > 3:
                data_mapping_dict.update({
                    'l1d_reg_misses': 4,
                    'l1d_irreg_misses': 6,
                })

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] = -float(tokens[v])

            cpi_result_struct['l1d_loads'] += float(tokens[0])
        elif re.match('WRITEBACK', line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'l1d_accesses': 0,
                'l1d_hits': 1,
                'l1d_misses': 2,
                'l1d_reg_hits': 3,
                'l1d_reg_misses': 4,
                'l1d_irreg_hits': 5,
                'l1d_irreg_misses': 6,
            }

            if len(tokens) > 3:
                data_mapping_dict.update({
                    'l1d_reg_misses': 4,
                    'l1d_irreg_misses': 6,
                })

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] -= float(tokens[v])
        elif re.match('TOTAL', line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            parser_state = DISTILL_CACHE_METRICS_STATE_2

            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'l1d_accesses': 0,
                'l1d_hits': 1,
                'l1d_misses': 2,
                'l1d_reg_hits': 3,
                'l1d_reg_misses': 4,
                'l1d_irreg_hits': 5,
                'l1d_irreg_misses': 6,
            }

            if len(tokens) > 3:
                data_mapping_dict.update({
                    'l1d_reg_misses': 4,
                    'l1d_irreg_misses': 6,
                })

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] += float(tokens[v])
        elif re.match('Average miss latency', line) and parser_state == DISTILL_CACHE_METRICS_STATE_2:
            # parser_state = DISTILL_CACHE_METRICS_STATE_3
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            if len(tokens) > 0:
                cpi_result_struct['l1d_avg_miss_latency'] = float(tokens[0])
        elif re.match('Bypass prediction', line) and parser_state == DISTILL_CACHE_METRICS_STATE_2:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'l1d_bypass': 0,
                'l1d_no_bypass': 1,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] += float(tokens[v])
        elif re.match('LMP stats', line) and parser_state == DISTILL_CACHE_METRICS_STATE_2:
            parser_state = DISTILL_CACHE_METRICS_STATE_3
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'accurate': 0,
                'inaccurate': 1,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct['l1d_bypass_predictor'][k] = float(tokens[v])

    # Going back at the beginning of the file. Reading L2C stats.
    data_fd.seek(0, 0)

    parser_state = DISTILL_CACHE_METRICS_STATE_0

    for line in data_fd.readlines():
        if parser_state == DISTILL_CACHE_METRICS_STATE_3:
            break
        elif re.match(r'\[L2C metrics\]', line) and parser_state == DISTILL_CACHE_METRICS_STATE_0:
            parser_state = DISTILL_CACHE_METRICS_STATE_1
            continue
        elif re.match('PREFETCH', line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'l2c_accesses': 0,
                'l2c_hits': 1,
                'l2c_misses': 2,
                'l2c_reg_hits': 3,
                'l2c_reg_misses': 4,
                'l2c_irreg_hits': 5,
                'l2c_irreg_misses': 6,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] -= float(tokens[v])

            cpi_result_struct['l2c_loads'] += float(tokens[0])
        elif re.match('LOAD', line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'l2c_loads': 0,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] = float(tokens[v])
        elif re.match('RFO', line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'l2c_stores': 0,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] = float(tokens[v])
        elif re.match('WRITEBACK', line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'l2c_accesses': 0,
                'l2c_hits': 1,
                'l2c_misses': 2,
                'l2c_reg_hits': 3,
                'l2c_reg_misses': 4,
                'l2c_irreg_hits': 5,
                'l2c_irreg_misses': 6,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] -= float(tokens[v])
        elif re.match('TOTAL', line) and parser_state == DISTILL_CACHE_METRICS_STATE_1:
            parser_state = DISTILL_CACHE_METRICS_STATE_2

            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'l2c_accesses': 0,
                'l2c_hits': 1,
                'l2c_misses': 2,
                'l2c_reg_hits': 3,
                'l2c_reg_misses': 4,
                'l2c_irreg_hits': 5,
                'l2c_irreg_misses': 6,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] += float(tokens[v])
        elif re.match(r'Average miss latency', line) and parser_state == DISTILL_CACHE_METRICS_STATE_2:
            parser_state = DISTILL_CACHE_METRICS_STATE_3
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            if len(tokens) > 0:
                cpi_result_struct['l2c_avg_miss_latency'] = float(tokens[0])
        # elif re.match(r'pf_requested', line) and parser_state == DISTILL_CACHE_METRICS_STATE_2:
        #     parser_state = DISTILL_CACHE_METRICS_STATE_3
        #     tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

        #     data_mapping_dict = {
        #         'l2c_pf_requested': 0,
        #         'l2c_pf_useful': 1,
        #         'l2c_pf_useless': 2,
        #     }

        #     for k, v in data_mapping_dict.items():
        #         cpi_result_struct[k] = int(tokens[v])

    # Going back at the beginning of the file.
    data_fd.seek(0, 0)

    parser_state = REQUESTED_SIZES_STATE_0

    for line in data_fd.readlines():
        if parser_state == REQUESTED_SIZES_STATE_2:
            break
        elif re.match(r'\[REQUESTED SIZES\]', line) and parser_state == REQUESTED_SIZES_STATE_0:
            parser_state = REQUESTED_SIZES_STATE_1
            continue
        elif (re.match(r'\[END REQUESTED SIZES\]', line) or re.match(r'^\s*$', line)) and parser_state == REQUESTED_SIZES_STATE_1:
            parser_state = REQUESTED_SIZES_STATE_2
            continue
        elif parser_state == REQUESTED_SIZES_STATE_0:
            continue

        # Doing the actual parsing here.
        tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

        cpi_result_struct['requested_sizes'][int(
            tokens[0]) - 1] = int(tokens[1])

    # Going back at the beginning of the file.
    data_fd.seek(0, 0)

    parser_state = WOC_REUSE_STATE_0

    for line in data_fd.readlines():
        if parser_state == WOC_REUSE_STATE_2:
            break
        elif re.match(r'\[WOC REUSE STATS\]', line) and parser_state == WOC_REUSE_STATE_0:
            parser_state = WOC_REUSE_STATE_1
            continue
        elif re.match(r'\[END WOC REUSE STATS\]', line) and parser_state == WOC_REUSE_STATE_1:
            parser_state = WOC_REUSE_STATE_2
            continue
        elif parser_state == WOC_REUSE_STATE_0:
            continue

        # Doing the actual parsing job.
        tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

        cpi_result_struct['woc_reuses'][int(tokens[0])] = int(tokens[1])

    # Going back at the beginning of the file.
    data_fd.seek(0, 0)

    parser_state = SDC_CACHE_METRICS_STATE_0

    for line in data_fd.readlines():
        if parser_state == SDC_CACHE_METRICS_STATE_3:
            break
        elif re.match(r'\[SDC metrics\]', line) and parser_state == SDC_CACHE_METRICS_STATE_0:
            parser_state = SDC_CACHE_METRICS_STATE_1
            continue
        elif re.match('PREFETCH', line) and parser_state == SDC_CACHE_METRICS_STATE_1:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'sdc_accesses': 0,
                'sdc_hits': 1,
                'sdc_misses': 2,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] = float(tokens[v])
        elif re.match('LOAD', line) and parser_state == SDC_CACHE_METRICS_STATE_1:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'sdc_loads': 0,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] = float(tokens[v])
        elif re.match('RFO', line) and parser_state == SDC_CACHE_METRICS_STATE_1:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'sdc_stores': 0,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] = float(tokens[v])
        elif re.match('WRITEBACK', line) and parser_state == SDC_CACHE_METRICS_STATE_1:
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'sdc_accesses': 0,
                'sdc_hits': 1,
                'sdc_misses': 2,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] -= float(tokens[v])
        elif re.match('TOTAL', line) and parser_state == SDC_CACHE_METRICS_STATE_1:
            parser_state = SDC_CACHE_METRICS_STATE_2

            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            data_mapping_dict = {
                'sdc_accesses': 0,
                'sdc_hits': 1,
                'sdc_misses': 2,
            }

            for k, v in data_mapping_dict.items():
                cpi_result_struct[k] += float(tokens[v])
        elif re.match('Average miss latency', line) and parser_state == SDC_CACHE_METRICS_STATE_2:
            parser_state = SDC_CACHE_METRICS_STATE_3
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            if len(tokens) > 0:
                cpi_result_struct['sdc_avg_miss_latency'] = float(tokens[0])

    # Going back at the beginning of the file. Reading routing engine accuracy metrics.
    data_fd.seek(0, 0)

    parser_state = SDC_RE_ACCURACY_STATE_0

    for line in data_fd.readlines():
        if parser_state == SDC_RE_ACCURACY_STATE_4:
            break
        elif re.match(r'Routing engine accuracy', line) and parser_state == SDC_RE_ACCURACY_STATE_0:
            parser_state = SDC_RE_ACCURACY_STATE_1

            # Parsing the line.
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)
            cpi_result_struct['routing_engine']['accuracy']['global'] = float(
                tokens[0]) if len(tokens) > 0 else 0.0
            continue
        elif re.match(r'SDC->DRAM', line) and parser_state == SDC_RE_ACCURACY_STATE_1:
            parser_state = SDC_CACHE_METRICS_STATE_2

            # Parsing the line.
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)
            cpi_result_struct['routing_engine']['accuracy']['dram'] = float(
                tokens[-1]) if not math.isnan(float(tokens[-1])) else 0.0
        elif re.match('SDC->L2C', line) and parser_state == SDC_RE_ACCURACY_STATE_2:
            parser_state = SDC_RE_ACCURACY_STATE_3

            # Parsing the line.
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)
            cpi_result_struct['routing_engine']['accuracy']['l2c'] = float(
                tokens[-1]) if not math.isnan(float(tokens[-1])) else 0.0
        elif re.match('SDC->LLC', line) and parser_state == SDC_RE_ACCURACY_STATE_3:
            parser_state = SDC_RE_ACCURACY_STATE_4

            # Parsing the line.
            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)
            cpi_result_struct['routing_engine']['accuracy']['llc'] = float(
                tokens[-1]) if not math.isnan(float(tokens[-1])) else 0.0
            # Going back at the beginning of the file.

    data_fd.seek(0, 0)

    parser_state = REQUESTS_STATE_0

    for line in data_fd.readlines():
        if parser_state == REQUESTS_STATE_6:
            tokens = line.split(' ')

            if tokens[0] == 'SDC':
                cpi_result_struct['served_from']['SDC'] = int(tokens[1])

            break
        elif re.match(r'CORE REQUESTS SERVED FROM', line) and parser_state == REQUESTS_STATE_0:
            parser_state = REQUESTS_STATE_1
            continue
        elif parser_state == REQUESTS_STATE_1:
            tokens = line.split(' ')

            if tokens[0] == '' or tokens[0] == 'Reg.':
                continue

            # Finish parsing.
            if tokens[0] == '\n':
                parser_state = REQUESTS_STATE_6
                continue

            cpi_result_struct['served_from'][tokens[0]] = int(tokens[1])

            continue

    # STLB general stats.
    data_fd.seek(0, 0)

    for line in data_fd.readlines():
        r_stlb_pattern = STLB_LOAD_PATTERN_RX.search(line)
        r_doa_pattern = DOA_PATTERN_RX.search(line)

        if r_stlb_pattern:
            cpi_result_struct['stlb']['accesses'] = int(
                r_stlb_pattern.group(1))
            cpi_result_struct['stlb']['misses'] = int(r_stlb_pattern.group(3))
        elif r_doa_pattern:
            cpi_result_struct['stlb']['doa'] = int(r_doa_pattern.group(1))
            cpi_result_struct['stlb']['non_doa'] = int(r_doa_pattern.group(2))

            break

    # STLB DOA & alive stats.
    data_fd.seek(0, 0)

    for line in data_fd.readlines():
        r_stlb_pattern = STLB_DOA_PATTERN_RX.search(line)

        if r_stlb_pattern:
            cpi_result_struct['stlb']['irreg_doa'] = int(
                r_stlb_pattern.group(1))
            cpi_result_struct['stlb']['irreg_alive'] = int(
                r_stlb_pattern.group(2))
            cpi_result_struct['stlb']['reg_doa'] = int(r_stlb_pattern.group(3))
            cpi_result_struct['stlb']['reg_alive'] = int(
                r_stlb_pattern.group(4))

    # Gathering data on the accuracy of the irregular accesses predictor.
    data_fd.seek(0, 0)

    for line in data_fd.readlines():
        r_irreg_pred = IRREG_ACCESS_PREDICTOR_ACCURACY_PATTERN_RX.search(line)
        r_irreg_stats = IRREG_ACCESS_PREDICTOR_TABLE_PATTERN_RX.search(line)

        if r_irreg_pred:
            cpi_result_struct['irregular_accesses']['accurate'] = int(
                r_irreg_pred.group(1))
            cpi_result_struct['irregular_accesses']['inaccurate'] = int(
                r_irreg_pred.group(2))
        elif re.match(r'IRREG_PRED', line):

            tokens = NUMERIC_CONST_PATTERN_RX.findall(line)

            cpi_result_struct['irregular_predictor']['accesses'] = int(
                tokens[0])
            cpi_result_struct['irregular_predictor']['hits'] = int(tokens[1])
            cpi_result_struct['irregular_predictor']['misses'] = int(tokens[2])
            cpi_result_struct['irregular_predictor']['changes'] = int(
                tokens[3])

            if len(tokens) > 4:
                cpi_result_struct['l1d_bypass_predictor']['accurate'] = float(
                    tokens[4])
                cpi_result_struct['l1d_bypass_predictor']['inaccurate'] = float(
                    tokens[5])

    # Gathering data on the MetaData Cache (Reducing Load Latency with Cache Level Prediction).
    data_fd.seek(0, 0)

    for line in data_fd.readlines():
        r_metadata_cache = METADATA_CACHE_PATTERN_RX.search(line)
        r_locmap_outcome = LOCMAP_PREDICTION_OUTCOME_PATTERN_RX.search(line)

        if r_metadata_cache:
            cpi_result_struct['metadata_cache']['hits'] = int(
                r_metadata_cache.group(1))
            cpi_result_struct['metadata_cache']['misses'] = int(
                r_metadata_cache.group(2))
            cpi_result_struct['metadata_cache']['miss_rate'] = cpi_result_struct['metadata_cache']['misses'] / (
                cpi_result_struct['metadata_cache']['misses'] + cpi_result_struct['metadata_cache']['hits'])
        elif r_locmap_outcome:
            prediction, perfect, count = r_locmap_outcome.group(
                1), r_locmap_outcome.group(2), int(r_locmap_outcome.group(3))

            cpi_result_struct['locmap'][prediction][perfect] = count

    # Gathering data on the MetaData Cache (Reducing Load Latency with Cache Level Prediction).
    data_fd.seek(0, 0)

    offchip_pred_dict = cpi_result_struct['offchip_pred']

    for line in data_fd.readlines():
        tokens = line.split()
        if len(tokens) == 0:
            continue

        if tokens[0] == 'perc_true_pos':
            offchip_pred_dict['true_pos'] = int(tokens[1])
        elif tokens[0] == 'perc_false_pos':
            offchip_pred_dict['false_pos'] = int(tokens[1])
        elif tokens[0] == 'perc_false_neg':
            offchip_pred_dict['false_neg'] = int(tokens[1])
        elif tokens[0] == 'perc_true_neg':
            offchip_pred_dict['true_neg'] = int(tokens[1])
        # From here we are considering the accuracy stats relating to the prefetch specific predictor.
        elif tokens[0] == 'perc_true_pos_pf':
            offchip_pred_dict['true_pos_pf'] = int(tokens[1])
        elif tokens[0] == 'perc_false_pos_pf':
            offchip_pred_dict['false_pos_pf'] = int(tokens[1])
        elif tokens[0] == 'perc_false_neg_pf':
            offchip_pred_dict['false_neg_pf'] = int(tokens[1])
        elif tokens[0] == 'perc_true_neg_pf':
            offchip_pred_dict['true_neg_pf'] = int(tokens[1])
        elif tokens[0] == 'miss_hit_l1d':
            offchip_pred_dict['miss_hit_l1d'] = int(tokens[1])
        elif tokens[0] == 'miss_hit_l2c':
            offchip_pred_dict['miss_hit_l2c'] = int(tokens[1])
            
            offchip_pred_dict = cpi_result_struct['l1d_offchip_pred']

    # print(data_fd, offchip_pred_dict)

    # Gathering data on the aggreement rate between Hermes and PP.
    data_fd.seek(0, 0)

    for line in data_fd.readlines():
        r_hermes_pp_aggreements = HERMES_PP_AGREEMENTS_PATTERN_RX.search(line)

        if r_hermes_pp_aggreements:
            cpi_result_struct['offchip_pred_hybrid']['aggreements_check'] = int(
                r_hermes_pp_aggreements.group(1))
            cpi_result_struct['offchip_pred_hybrid']['aggreements'] = int(
                r_hermes_pp_aggreements.group(2))

    data_fd.seek(0, 0)

    for line in data_fd.readlines():
        r_dram_transactions = DRAM_TRANSACTIONS_RX.search(line)

        if r_dram_transactions:
            cpi_result_struct['dram']['transactions'] += int(
                r_dram_transactions.group(1))
            cpi_result_struct['dram']['transactions'] += int(
                r_dram_transactions.group(2))

    # Gathering data on the number of L1D hits that saved an Hermes request.
    data_fd.seek(0, 0)

    for line in data_fd.readlines():
        r_l1d_saved_requests = HERMES_PP_L1D_SAVED_HERMES_REQUEST_PATTERN_RX.search(
            line)
        r_l2c_saved_requests = HERMES_PP_L2C_SAVED_HERMES_REQUEST_PATTERN_RX.search(
            line)
        r_llc_saved_requests = HERMES_PP_LLC_SAVED_HERMES_REQUEST_PATTERN_RX.search(
            line)

        if r_l1d_saved_requests:
            cpi_result_struct['l1d_offchip_pred']['l1d_saved_hermes_requests'] = int(
                r_l1d_saved_requests.group(1))
        if r_l2c_saved_requests:
            cpi_result_struct['l1d_offchip_pred']['l2c_saved_hermes_requests'] = int(
                r_l2c_saved_requests.group(1))
        if r_llc_saved_requests:
            cpi_result_struct['l1d_offchip_pred']['llc_saved_hermes_requests'] = int(
                r_llc_saved_requests.group(1))

    # Gathering data on the number of PCs that threw memory accesses to the cache hierarchy.
    data_fd.seek(0, 0)

    for line in data_fd.readlines():
        r_memory_access_pcs = MEMORY_ACCESS_PC_PATTERN_RX.search(line)

        if r_memory_access_pcs:
            cpi_result_struct['memory_access_pc'] = int(
                r_memory_access_pcs.group(1))

    # Gathering data on prefetchers.
    data_fd.seek(0, 0)
    file_content = data_fd.read()

    for id, match in enumerate(PREFETCHERS_STATS_RX.finditer(file_content)):
        if id == 0 or id == 3:  # L1I or SDC --> We do not care about these.
            pass
        elif id == 1:  # L1D
            cpi_result_struct['l1d_prefetcher']['pf_issued'] = int(
                match.group('pf_issued'))
            cpi_result_struct['l1d_prefetcher']['pf_fill'] = int(
                match.group('pf_fill'))
            cpi_result_struct['l1d_prefetcher']['pf_useful'] = int(
                match.group('pf_useful'))
        elif id == 2:  # L2C
            cpi_result_struct['l2c_prefetcher']['pf_issued'] = int(
                match.group('pf_issued'))
            cpi_result_struct['l2c_prefetcher']['pf_fill'] = int(
                match.group('pf_fill'))
            cpi_result_struct['l2c_prefetcher']['pf_useful'] = int(
                match.group('pf_useful'))
        elif id == 4:  # LLC
            cpi_result_struct['llc_prefetcher']['pf_issued'] = int(
                match.group('pf_issued'))
            cpi_result_struct['llc_prefetcher']['pf_fill'] = int(
                match.group('pf_fill'))
            cpi_result_struct['llc_prefetcher']['pf_useful'] = int(
                match.group('pf_useful'))
    
    # Getting data on the location from which useful/useless prefetches were served.
    r_useful_useless_loc = L1D_PREFETCHERS_LOC_STATS_RX.search(file_content)

    if r_useful_useless_loc:
        cpi_result_struct['l1d_prefetcher']['useful']['l2c'] = int(r_useful_useless_loc.group('pf_useful_l2c'))
        cpi_result_struct['l1d_prefetcher']['useful']['llc'] = int(r_useful_useless_loc.group('pf_useful_llc'))
        cpi_result_struct['l1d_prefetcher']['useful']['dram'] = int(r_useful_useless_loc.group('pf_useful_dram'))

        cpi_result_struct['l1d_prefetcher']['useless']['l2c'] = int(r_useful_useless_loc.group('pf_useless_l2c'))
        cpi_result_struct['l1d_prefetcher']['useless']['llc'] = int(r_useful_useless_loc.group('pf_useless_llc'))
        cpi_result_struct['l1d_prefetcher']['useless']['dram'] = int(r_useful_useless_loc.group('pf_useless_dram'))

    # Setting the valid value.
    valid = valid & (cpi_result_struct['cpi'] > 0.0)
    # valid = valid & (len(cpi_result_struct['woc_reuses']) > 0)

    return valid, cpi_result_struct


def multicore_cache_parser(data_fd):
    # Let's build some regular expressions to extract the relevant data.
    re_multicore_ipc, re_dram = re.compile(r'^CPU (\d) cumulative IPC: (\d+\.\d+)'), \
        re.compile(r'RQ ROW_BUFFER_HIT: (\d+)\s+ROW_BUFFER_MISS: (\d+)')

    re_l1d_stats = re.compile(r"""^\[L1D metrics\]
.+
LOAD\s+(\d+).+
RFO\s+(\d+).+
PREFETCH\s+(\d+).+
WRITEBACK\s+(\d+).+$""", re.MULTILINE)

    re_l2c_stats = re.compile(r"""^\[L2C metrics\]
.+
LOAD\s+(\d+).+
RFO\s+(\d+).+
PREFETCH\s+(\d+).+
WRITEBACK\s+(\d+).+$""", re.MULTILINE)

    re_llc_stats = re.compile(r"""^\[LLC metrics\]
.+
LOAD\s+(\d+).+
RFO\s+(\d+).+
PREFETCH\s+(\d+).+
WRITEBACK\s+(\d+).+$""", re.MULTILINE)

    struct_data = {
        'ipcs': {},  # A dictionnary of IPCs, one entry per CPU.
        'dram': {
            'transactions': 0
        },

        # WIP: In order to use the energy models, we have to capture the number of loads and stores in each cache of the chip.
        'caches': {
            'l1d': [],
            'l2c': [],
            'llc': [],
        },

        'energy_models': {
            'l1d': CacheEnergyModel('l1d', 0.323109, 0.329212, 19.9591),
            'l2c': CacheEnergyModel('l2c', 0.278033, 0.301787, 366.279),
            'llc': CacheEnergyModel('llc', 0.477679, 0.560414, 2858.79),
        },
        'energy': [],
        'total_energy': 0.0
    }

    for line in reversed(data_fd.readlines()):
        re_ipc_res, re_dram_trans_res = re_multicore_ipc.search(line), \
            re_dram.search(line)

        # A line containing the IPC of a CPU has been found.
        if re_ipc_res is not None:
            cpu_id, ipc = int(re_ipc_res.group(1)), float(re_ipc_res.group(2))

            # Updating the dictionary of IPCs.
            struct_data['ipcs'][cpu_id] = ipc

            # As we read backwards, when the CPU id turns out to be zero, the parsing is over.
            if cpu_id == 0:
                break
        elif re_dram_trans_res is not None:
            struct_data['dram']['transactions'] += int(
                re_dram_trans_res.group(1))
            struct_data['dram']['transactions'] += int(
                re_dram_trans_res.group(2))

    data_fd.seek(0, 0)

    file_content = data_fd.read()

    for cpu_id, match in enumerate(re_l1d_stats.finditer(file_content)):
        l1d_cache_stats = {
            'loads': int(match.group(1)) + int(match.group(3)),
            'stores': int(match.group(2)) + int(match.group(4)),
        }

        struct_data['caches']['l1d'].insert(0, l1d_cache_stats)

    for cpu_id, match in enumerate(re_l2c_stats.finditer(file_content)):
        l2c_cache_stats = {
            'loads': int(match.group(1)) + int(match.group(3)),
            'stores': int(match.group(2)) + int(match.group(4)),
        }

        struct_data['caches']['l2c'].insert(0, l2c_cache_stats)

    for cpu_id, match in enumerate(re_llc_stats.finditer(file_content)):
        llc_cache_stats = {
            'loads': int(match.group(1)) + int(match.group(3)),
            'stores': int(match.group(2)) + int(match.group(4)),
        }

        struct_data['caches']['llc'].insert(0, llc_cache_stats)

    return len(struct_data['ipcs']) > 0, struct_data

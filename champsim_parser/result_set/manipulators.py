#!/usr/bin/env python3

from inspect import trace
import os
import re
from collections import OrderedDict
from functools import reduce
from operator import add

import numpy as np
import pandas as pd
import scipy.stats
from gpg._gpgme import _gpgme_op_decrypt_result_session_key_set

from champsim_parser.result_set import ResultSet
from scipy.stats import gmean


def get_sim_points(sim_points_path):
    """

    :param sim_points_path:
    :return:
    """
    weights = {}

    for root, dirs, files in os.walk(f'{sim_points_path}'):
        for filename in files:
            if filename != 'concat.txt':
                continue

            bench_name = root.split('/')[-1]

            with open(f'{root}/{filename}') as weight_file:
                line = weight_file.readline()

                while line:
                    tokens = line.split(';')

                    if bench_name not in weights:
                        weights[bench_name] = {}

                    weights[bench_name][tokens[0].strip()] = float(
                        tokens[1].strip())

                    line = weight_file.readline()

    return weights


def normalize_llc_misses(results_set):
    # Here, we simply compute the LLC MPKI for each entry of the results set.
    config = results_set.config

    for k, v in results_set.items():
        v['llc_mpki'] = v['llc_misses'] / \
            (config['simulation_instructions'] * 1000.0)

    # We do not return anything, just modifying sets in place.
    return


def normalize_llc_distill_cache(results_set, ref_served_from=None):
    config = results_set.config
    ref = ref_served_from if ref_served_from is not None else results_set

    l1d_hit_latency, sdc_hit_latency = 4, 4

    if config['bin'] == 'sectored_cache_sdc_256KB_6cycles':
        sdc_hit_latency = 6

    for k, v in results_set.items():
        total_served_requests = 0

        v.update({
            # L1D related stats.
            'l1d_hits_pki': v['l1d_hits'] / (config['simulation_instructions'] * 1000.0),
            'l1d_reg_hits_pki': v['l1d_reg_hits'] / (config['simulation_instructions'] * 1000.0),
            'l1d_irreg_hits_pki': v['l1d_irreg_hits'] / (config['simulation_instructions'] * 1000.0),
            'l1d_mpki': v['l1d_misses'] / (config['simulation_instructions'] * 1000.0),
            'l1d_reg_mpki': v['l1d_reg_misses'] / (config['simulation_instructions'] * 1000.0),
            'l1d_irreg_mpki': v['l1d_irreg_misses'] / (config['simulation_instructions'] * 1000.0),

            # L2C related stats.
            'l2c_hits_pki': v['l2c_hits'] / (config['simulation_instructions'] * 1000.0),
            'l2c_reg_hits_pki': v['l2c_reg_hits'] / (config['simulation_instructions'] * 1000.0),
            'l2c_irreg_hits_pki': v['l2c_irreg_hits'] / (config['simulation_instructions'] * 1000.0),
            'l2c_mpki': v['l2c_misses'] / (config['simulation_instructions'] * 1000.0),
            'l2c_reg_mpki': v['l2c_reg_misses'] / (config['simulation_instructions'] * 1000.0),
            'l2c_irreg_mpki': v['l2c_irreg_misses'] / (config['simulation_instructions'] * 1000.0),

            # SDC related stats.
            'sdc_hits_pki': v['sdc_hits'] / (config['simulation_instructions'] * 1000.0),
            'sdc_mpki': v['sdc_misses'] / (config['simulation_instructions'] * 1000.0),

            # LLL related stats.
            'llc_misses': v['llc_hole_miss'] + v['llc_line_miss'],
            'llc_accesses_pki': v['llc_accesses'] / (config['simulation_instructions'] * 1000.0),
            'llc_loc_hit_pki': v['llc_loc_hit'] / (config['simulation_instructions'] * 1000.0),
            'llc_woc_hit_pki': v['llc_woc_hit'] / (config['simulation_instructions'] * 1000.0),
            'llc_hole_miss_pki': v['llc_hole_miss'] / (config['simulation_instructions'] * 1000.0),
            'llc_line_miss_pki': v['llc_line_miss'] / (config['simulation_instructions'] * 1000.0),
            'reg_mpki': v['reg_misses'] / (config['simulation_instructions'] * 1000.0),
            'irreg_mpki': v['irreg_misses'] / (config['simulation_instructions'] * 1000.0),
        })

        # Updating L1D Bypass predictor stats.
        v['l1d_bypass_predictor'].update({
            'accuracy': v['l1d_bypass_predictor']['accurate'] / (v['l1d_bypass_predictor']['accurate'] + v['l1d_bypass_predictor']['inaccurate']) if v['l1d_bypass_predictor']['accurate'] + v['l1d_bypass_predictor']['inaccurate'] > 0.0 else 0.0
        })

        # Updating caches hit rates.
        v.update({
            'l1d_hit_rate': 0.0 if v['l1d_accesses'] == 0 else v['l1d_hits'] / v['l1d_accesses'],
            'l1d_miss_rate': 0.0 if v['l1d_accesses'] == 0 else v['l1d_misses'] / v['l1d_accesses'],
            'sdc_hit_rate': 0.0 if v['sdc_accesses'] == 0 else v['sdc_hits'] / v['sdc_accesses'],
            'sdc_miss_rate': 0.0 if v['sdc_accesses'] == 0 else v['sdc_misses'] / v['sdc_accesses'],

            'l1d_reg_hit_rate': 0.0 if v['l1d_reg_hits'] == 0 else (v['l1d_reg_hits'] / (v['l1d_reg_hits'] + v['l1d_reg_misses'])),
            'l1d_irreg_hit_rate': 0.0 if v['l1d_irreg_hits'] == 0 else (v['l1d_irreg_hits'] / (v['l1d_irreg_hits'] + v['l1d_irreg_misses'])),

            'l2c_hit_rate': 0.0 if v['l2c_accesses'] == 0 else v['l2c_hits'] / v['l2c_accesses'],
            'l2c_reg_hit_rate': 0.0 if v['l2c_reg_hits'] == 0 else (v['l2c_reg_hits'] / (v['l2c_reg_hits'] + v['l2c_reg_misses'])),
            'l2c_irreg_hit_rate': 0.0 if v['l2c_irreg_hits'] == 0 else (v['l2c_irreg_hits'] / (v['l2c_irreg_hits'] + v['l2c_irreg_misses'])),

            'llc_hit_rate': 0.0 if v['llc_accesses'] == 0 else v['llc_loc_hit'] / v['llc_accesses'],
            'llc_reg_hit_rate': 0.0 if v['reg_hits'] == 0 else (v['reg_hits'] / (v['reg_hits'] + v['reg_misses'])),
            'llc_irreg_hit_rate': 0.0 if v['irreg_hits'] == 0 else (v['irreg_hits'] / (v['irreg_hits'] + v['irreg_misses'])),
        })

        # Updating cache misses cost.
        v.update({
            'front_end_miss_cost': 0.0 if (v['l1d_misses'] + v['sdc_misses']) == 0 else (v['l1d_misses'] * v['l1d_avg_miss_latency'] + v['sdc_misses'] * v['sdc_avg_miss_latency']) / (v['l1d_misses'] + v['sdc_misses'])
        })

        # Updating the AMAT metric.
        v.update({
            'amat_l1d': l1d_hit_latency * v['l1d_hit_rate'] + v['l1d_miss_rate'] * v['l1d_avg_miss_latency'],
            'amat_sdc': sdc_hit_latency * v['sdc_hit_rate'] + v['sdc_miss_rate'] * v['sdc_avg_miss_latency'],
        })

        # Now, we also have to normalize the accuracy stats of the irregular accesses predictor.
        v['irregular_accesses'].update({
            'accuracy': v['irregular_accesses']['accurate'] / max(reduce(add, v['irregular_accesses'].values()), 1)
        })

        v['irregular_predictor'].update({
            'mpki': v['irregular_predictor']['misses'] / (config['simulation_instructions'] * 1000.0),
            'change_rate': 0.0 if v['irregular_predictor']['accesses'] == 0.0 else v['irregular_predictor']['changes'] / v['irregular_predictor']['accesses'],
        })

        v['metadata_cache'].update({
            'mpki': v['metadata_cache']['misses'] / (config['simulation_instructions'] * 1000.0),
        })

        v['stlb'].update({
            'mpki': v['stlb']['misses'] / (config['simulation_instructions'] * 1000.0),
        })

        # TODO: Here we should convert the LocMap predictor outcome to a matrix.
        wdict = {k: list(v.values()) for k, v in v['locmap'].items()}
        outcome_df = pd.DataFrame.from_dict(wdict, orient='index', columns=[
                                            'L2C', 'LLC', 'L2C + LLC', 'DRAM']).transpose()

        v['locmap']['outcome_map'] = outcome_df

        # If the matrix is full of zero it means that no predictions have been made. Accuracy is considered 100% here.
        if not np.any(outcome_df):
            v['locmap']['accuracy'] = 1.0
        else:
            v['locmap']['accuracy'] = (np.sum(np.diag(outcome_df)) + outcome_df['L2C + LLC']['L2C'] + outcome_df['L2C']
                                       ['L2C + LLC'] + outcome_df['L2C + LLC']['LLC'] + outcome_df['LLC']['L2C + LLC']) / np.sum(outcome_df.to_numpy())

        # TODO: We should do a pass on this one and implement it again in a clean way.
        # v.update({
        #     'amat': (v['l1d_accesses'] * v['amat_l1d'] + v['sdc_accesses'] * v['amat_sdc']) / (v['l1d_accesses'] + v['sdc_accesses']),
        # })

        # Computing the accuracy of the offchip predictor.
        offchip_predictions = reduce(
            add, [v for k, v in v['offchip_pred'].items() if '_pf' not in k])
        v['offchip_pred'].update({
            'accuracy': 1.0 if offchip_predictions == 0 else (v['offchip_pred']['true_pos'] + v['offchip_pred']['true_neg']) / offchip_predictions
        })

        # Computing the accuracy of the prefetch-specific offchip predictor.
        offchip_pf_predictions = reduce(
            add, [v for k, v in v['offchip_pred'].items() if '_pf' in k])
        v['offchip_pred'].update({
            'accuracy_pf': 1.0 if offchip_pf_predictions == 0 else (v['offchip_pred']['true_pos_pf'] + v['offchip_pred']['true_neg_pf']) / offchip_pf_predictions,
            # 'precision_pf': 1.0 if offchip_pf_predictions == 0 else v['offchip_pred']['true_pos_pf'] / (v['offchip_pred']['true_pos_pf'] + v['offchip_pred']['false_pos_pf'])
            'true_pos_rate': 0.0 if (v['offchip_pred']['true_pos'] + v['offchip_pred']['false_pos']) == 0 else v['offchip_pred']['true_pos'] / (v['offchip_pred']['true_pos'] + v['offchip_pred']['false_pos']),
            'miss_hit_l1d_rate': 0.0 if (v['offchip_pred']['true_pos'] + v['offchip_pred']['false_pos']) == 0 else v['offchip_pred']['miss_hit_l1d'] / (v['offchip_pred']['true_pos'] + v['offchip_pred']['false_pos']),
            'miss_hit_l2c_rate': 0.0 if (v['offchip_pred']['true_pos'] + v['offchip_pred']['false_pos']) == 0 else v['offchip_pred']['miss_hit_l2c'] / (v['offchip_pred']['true_pos'] + v['offchip_pred']['false_pos']),
            'miss_hit_l2c_llc_rate': 0.0 if (v['offchip_pred']['true_pos'] + v['offchip_pred']['false_pos']) == 0 else (v['offchip_pred']['false_pos'] - v['offchip_pred']['miss_hit_l1d'] - v['offchip_pred']['miss_hit_l2c']) / (v['offchip_pred']['true_pos'] + v['offchip_pred']['false_pos']),
        })

        l1d_offchip_predictions = reduce(add, v['l1d_offchip_pred'].values())
        v['l1d_offchip_pred'].update({
            'accuracy': 1.0 if l1d_offchip_predictions == 0 else (v['l1d_offchip_pred']['true_pos'] + v['l1d_offchip_pred']['true_neg']) / l1d_offchip_predictions,
        })

        # v['l1d_offchip_pred']['l1d_saved_hermes_requests'] = v['l1d_offchip_pred']['l1d_saved_hermes_requests'] / \
        #     (v['offchip_pred']['true_pos'] + v['offchip_pred']['true_neg'])
        # v['l1d_offchip_pred']['l2c_saved_hermes_requests'] = v['l1d_offchip_pred']['l2c_saved_hermes_requests'] / \
        #     (v['offchip_pred']['true_pos'] + v['offchip_pred']['true_neg'])
        # v['l1d_offchip_pred']['llc_saved_hermes_requests'] = v['l1d_offchip_pred']['llc_saved_hermes_requests'] / \
        #     (v['offchip_pred']['true_pos'] + v['offchip_pred']['true_neg'])

        # v['offchip_pred_hybrid']['aggreement_rate'] = 1.0 if v['offchip_pred_hybrid']['aggreements_check'] == 0 else v['offchip_pred_hybrid']['aggreements'] / \
        #     v['offchip_pred_hybrid']['aggreements_check']

        # l1d_pref_useful = reduce(
        #     add, [v for k, v in v['l1d_prefetcher']['useful'].items()])
        # l1d_pref_useless = reduce(
        #     add, [v for k, v in v['l1d_prefetcher']['useless'].items()])

        for l in ['l2c', 'llc', 'dram']:
            v['l1d_prefetcher']['useful'][l] /= (config['simulation_instructions'] * 1000.0)
            v['l1d_prefetcher']['useless'][l] /= (config['simulation_instructions'] * 1000.0)
        total_served_requests = reduce(add, ref[k]['served_from'].values())

        if total_served_requests == 0.0:
            continue

        for cache in v['served_from'].keys():
            v['served_from'][cache] /= total_served_requests

        # Computing energy metrics based on the model extracted from CACTI.
        caches, models = results_set.cache_energy_models.keys(
        ), results_set.cache_energy_models.values()
        load_keys, store_keys = \
            [f'{k}_loads' for k in caches], [f'{k}_stores' for k in caches]

        for c, m, lk, sk in zip(caches, models, load_keys, store_keys):
            param_dict = {
                'loads': v[lk],
                'stores': v[sk],
                'time': v['cpi'] * (results_set.config['simulation_instructions'] * (10 ** 6)) / (3.8 * (10 ** 9))
            }

            v[c] = {}

            cache_energy = m.compute(**param_dict)

            v[c].update({
                'static_energy': cache_energy['static_energy'],
                'dynamic_energy': cache_energy['dynamic_energy'],
                'energy': cache_energy['static_energy'] + cache_energy['dynamic_energy']
            })

        # Computing the total energy.
        v['static_energy'] = reduce(
            add, [v[c]['static_energy'] for c in caches])
        v['dynamic_energy'] = reduce(
            add, [v[c]['dynamic_energy'] for c in caches])
        v['energy'] = reduce(add, [v[c]['energy'] for c in caches])

        # Computing L1D prefetcher stats (accuracy, etc.)
        v['l1d_prefetcher']['accuracy'] = 1.0 if v['l1d_prefetcher']['pf_fill'] == 0 else v['l1d_prefetcher']['pf_useful'] / \
            v['l1d_prefetcher']['pf_fill']
        v['l2c_prefetcher']['accuracy'] = 1.0 if v['l2c_prefetcher']['pf_fill'] == 0 else v['l2c_prefetcher']['pf_useful'] / \
            v['l2c_prefetcher']['pf_fill']
        v['llc_prefetcher']['accuracy'] = 1.0 if v['llc_prefetcher']['pf_fill'] == 0 else v['llc_prefetcher']['pf_useful'] / \
            v['llc_prefetcher']['pf_fill']
        
        v['l1d_prefetcher']['coverage'] = 1.0 if v['l1d_misses'] == 0.0 else v['l1d_prefetcher']['pf_useful'] / v['l1d_misses']
        v['l2c_prefetcher']['coverage'] = 0.0 if v['l2c_misses'] == 0.0 else v['l2c_prefetcher']['pf_useful'] / v['l2c_misses']

        # Adjusting the misses metrics for the calculation of coverage.
        # v['l1d_prefetcher']['misses'] = 0.0 if v['l1d_misses'] < v['l1d_prefetcher']['pf_useful'] else v['l1d_misses'] - v['l1d_prefetcher']['pf_useful']
        v['l1d_prefetcher']['misses'] = v['llc_misses']
        v['l2c_prefetcher']['misses'] = v['l2c_misses'] - v['l2c_prefetcher']['pf_useful']


    normalize_llc_misses(results_set)

    return


def normalize_sdc_disill_cache(results_set):
    config = results_set.config

    for k, v in results_set.items():
        v.update({
            'sdc_mpki': v['sdc_misses'] / (config['simulation_instructions'] * 1000.0)
        })

    normalize_llc_misses(results_set)

    return


def normalize_irregular_llc_cache(results_set):
    config = results_set.config

    for k, v in results_set.items():
        v.update({
            'llc_accesses_pki': 1.0,
            'llc_loc_hit_pki': 1.0,
            'llc_woc_hit_pki': 1.0,
            'llc_hole_miss_pki': 1.0,
            'llc_line_miss_pki': 1.0,

            'woc_reuses': {},

            'requested_sizes': np.zeros(shape=8, dtype=int),

            'reg_accesses': v['reg_hits'] + v['reg_misses'],
            'irreg_accesses': v['irreg_hits'] + v['irreg_misses'],

            'llc_misses': v['irreg_misses'] + v['reg_misses'],

            'reg_mpki': v['reg_misses'] / (config['simulation_instructions'] * 1000.0),
            'irreg_mpki': v['irreg_misses'] / (config['simulation_instructions'] * 1000.0),
            'reg_hits_pki': v['reg_hits'] / (config['simulation_instructions'] * 1000.0),
            'irreg_hits_pki': v['irreg_hits'] / (config['simulation_instructions'] * 1000.0),
        })

    normalize_llc_misses(results_set)


def apply_simpoint(result_set, ref_set, simpoints_data, use=lambda n, d: True):
    """
    This result set manipulator applies the SimPoint methodology in terms of speed-up over a reference set of result.
    The manipulator eventually returns a new result set that will contain results after application of the SimPoints
    methodology.

    :param result_set:
    :param ref_set:
    :simpoints_data:
    :use: Boolean function that tells if the given benchmark should be used. It takes two parameters:
        - First the name of the workload that is currently processed.
        - Second, the details of it (e.g.: LLC MPKI, etc.).
    :n:
    :d:
    :return: A new result set with results for the SimPoints methodology.
    """
    # We create a brand new result set that we will return at the end of this function.
    simpoints_result_set = ResultSet(result_set.config)

    # We also need some temporary structures to maintain some of the required computations.
    #
    # We need two dictionaries:
    #   1. CPI using SimPoints methodology per benchmark
    #   2. CPI using SimPoints methodology per ref benchmark
    sp_cpi_benches, sp_cpi_ref_benches, weights_sum = {}, {}, {}
    sp_llc_mpki_benches, sp_llc_mpki_ref_benches = {}, {}
    sp_llc_loc_hit_pki_benches, sp_llc_loc_hit_pki_ref = {}, {}
    sp_llc_woc_hit_pki_benches, sp_llc_woc_hit_pki_ref = {}, {}
    sp_llc_hole_miss_pki_benches, sp_llc_hole_miss_pki_ref = {}, {}
    sp_llc_line_miss_pki_benches, sp_llc_line_miss_pki_ref = {}, {}
    sp_llc_woc_requested_sizes = {}
    sp_llc_woc_reuses = {}
    sp_sdc_hits_pki, sp_l1d_hits_pki, sp_l2c_hits_pki = {}, {}, {}
    sp_l1d_reg_hits_pki, sp_l2c_reg_hits_pki = {}, {}
    sp_l1d_irreg_hits_pki, sp_l2c_irreg_hits_pki = {}, {}
    sp_l1d_mpki, sp_l2c_mpki = {}, {}
    sp_l1d_reg_mpki, sp_l1d_irreg_mpki = {}, {}
    sp_l2c_reg_mpki, sp_l2c_irreg_mpki = {}, {}
    sp_llc_sdc_mpki = {}
    sp_llc_reg_misses, sp_llc_reg_mpki = {}, {}
    sp_llc_irreg_misses, sp_llc_irreg_mpki = {}, {}
    sp_llc_reg_hits, sp_llc_reg_hits_pki = {}, {}
    sp_llc_irreg_hits, sp_llc_irreg_hits_pki = {}, {}

    sp_sdc_hit_rate, sp_l1d_hit_rate, sp_l2c_hit_rate, sp_llc_hit_rate = {}, {}, {}, {}
    sp_l1d_reg_hit_rate, sp_l1d_irreg_hit_rate, sp_l1d_bypass, sp_l1d_no_bypass = {}, {}, {}, {}
    sp_bypass_pred_accuracy = {}
    sp_l2c_reg_hit_rate, sp_l2c_irreg_hit_rate = {}, {}
    sp_llc_reg_hit_rate, sp_llc_irreg_hit_rate = {}, {}

    sp_irreg_doa, sp_irreg_alive, sp_reg_doa, sp_reg_alive = \
        {}, {}, {}, {}

    sp_irreg_accesses_accuracy = {}
    sp_irreg_pred_mpki = {}
    sp_irreg_pred_change_rate = {}

    sp_served_from = {}

    sp_re_accuracy = {}

    sp_front_end_miss_cost, sp_ref_front_end_miss_cost = {}, {}
    sp_amat, sp_ref_amat = {}, {}
    sp_energy, sp_energy_ref = {}, {}
    sp_static_energy, sp_static_energy_ref = {},  {}
    sp_dynamic_energy, sp_dynamic_energy_ref = {},  {}

    sp_metadata_cache_miss_rate, sp_metadata_cache_mpki = {}, {}
    sp_locmap_accuracy = {}
    sp_locmap_outcome = {}

    sp_offchip_pred_accuracy, sp_offchip_pf_pred_accuracy, sp_l1d_offchip_pred_accuracy, sp_l1d_avoided_hermes, sp_l2c_avoided_hermes, sp_llc_avoided_hermes = {}, {}, {}, {}, {}, {}
    sp_offchip_miss_hit_l1d, sp_offchip_miss_hit_l2c, sp_offchip_miss_hit_l2c_llc, sp_offchip_true_pos_rate = {}, {}, {}, {}
    # sp_offchip_pf_precision = {}
    sp_memory_access_pcs = {}
    sp_hermes_pp_aggreement_rate = {}
    sp_dram_transactions = {}

    sp_stlb_mpki, sp_doa, sp_non_doa = {}, {}, {}

    sp_l1d_pref_issued, sp_l2c_pref_issued, sp_llc_pref_issued = {}, {}, {}
    sp_l1d_pref_acc, sp_l2c_pref_acc, sp_llc_pref_acc = {}, {}, {}
    sp_l1d_pref_cov, sp_l2c_pref_cov = {}, {}
    sp_l1d_pref_misses, sp_l2c_pref_misses = {}, {}
    sp_l1d_useful_pref_loc_l2c, sp_l1d_useful_pref_loc_llc, sp_l1d_useful_pref_loc_dram = {}, {}, {}
    sp_l1d_useless_pref_loc_l2c, sp_l1d_useless_pref_loc_llc, sp_l1d_useless_pref_loc_dram = {}, {}, {}

    # First, a few sanity checks.
    # if result_set.keys() != ref_set.keys():
    #     raise Exception(f'Reference & compared result set are different! {result_set.config}')

    # Now that we have checked elementary conditions, let's get in trouble with this!
    for k in result_set.keys():
        is_irreg_stats = 'irreg_mpki' in result_set[k]
        is_sdc_stats = 'sdc_mpki' in result_set[k]
        is_ligra, is_cloudsuite, is_qualcomm, tokens, trace_id = False, False, False, k.split(
            '-'), str()

        # if tokens[0] == '429.mcf' or tokens[0] == '619.lbm_s':
        #     continue

        is_ligra = tokens[0].split('_')[0] == 'ligra'

        if is_ligra:
            tokens[0] = ' '.join(tokens)
            simpoints_data[tokens[0]] = {0: 1.0}
            # print(tokens)

        # If there is only one tokens, it means that this was a trace from a CloudSuite benchmark.
        is_cloudsuite = len(tokens) == 1

        # Qualcomm traces are named following a really predictable patter, we just have to detect it.
        is_qualcomm = 'compute_fp' in tokens[0] or 'compute_int' in tokens[0] or 'server' in tokens[0] or 'srv' in \
                      tokens[0]

        # We differentiate the processing depending if it's a CloudSuite benchmark or not.
        if is_cloudsuite or is_qualcomm:
            trace_name = tokens[0].split('.')[0]
            if tokens[0] not in sp_cpi_ref_benches:
                sp_cpi_ref_benches[trace_name] = ref_set[k]['cpi']
                sp_cpi_benches[trace_name] = result_set[k]['cpi']

                sp_llc_mpki_benches[trace_name] = result_set[k]['llc_mpki']
                sp_llc_mpki_ref_benches[trace_name] = ref_set[k]['llc_mpki']

                # For the purpose of CloudSuite benchmarks, it is much simpler.
                weights_sum[trace_name] = 1.0
        else:
            # Computing the trace id.
            trace_id = tokens[1].split('.')[0][:-1]

            if is_ligra:
                # tokens[0] = '-'.join(tokens)
                trace_id = 0

            if tokens[0] not in sp_cpi_ref_benches:
                sp_cpi_ref_benches[tokens[0]] = ref_set[k]['cpi'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_cpi_benches[tokens[0]] = result_set[k]['cpi'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_llc_mpki_benches[tokens[0]] = result_set[k]['llc_mpki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_mpki_ref_benches[tokens[0]] = ref_set[k]['llc_mpki'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_llc_loc_hit_pki_benches[tokens[0]] = result_set[k]['llc_loc_hit_pki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_loc_hit_pki_ref[tokens[0]] = ref_set[k]['llc_loc_hit_pki'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_llc_woc_hit_pki_benches[tokens[0]] = result_set[k]['llc_woc_hit_pki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_woc_hit_pki_ref[tokens[0]] = result_set[k]['llc_woc_hit_pki'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_llc_hole_miss_pki_benches[tokens[0]] = result_set[k]['llc_hole_miss_pki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_hole_miss_pki_ref[tokens[0]] = ref_set[k]['llc_hole_miss_pki'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_llc_line_miss_pki_benches[tokens[0]] = result_set[k]['llc_line_miss_pki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_line_miss_pki_ref[tokens[0]] = ref_set[k]['llc_line_miss_pki'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_llc_woc_requested_sizes[tokens[0]] = np.array(
                    result_set[k]['requested_sizes']) * simpoints_data[tokens[0]][trace_id]

                sp_sdc_hit_rate[tokens[0]] = result_set[k]['sdc_hit_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l1d_hit_rate[tokens[0]] = result_set[k]['l1d_hit_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l2c_hit_rate[tokens[0]] = result_set[k]['l2c_hit_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_hit_rate[tokens[0]] = result_set[k]['llc_hit_rate'] * \
                    simpoints_data[tokens[0]][trace_id]

                if is_sdc_stats:
                    sp_l1d_hits_pki[tokens[0]] = result_set[k]['l1d_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l1d_reg_hits_pki[tokens[0]] = result_set[k]['l1d_reg_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l1d_irreg_hits_pki[tokens[0]] = result_set[k]['l1d_irreg_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l1d_mpki[tokens[0]] = np.array(
                        result_set[k]['l1d_mpki']) * simpoints_data[tokens[0]][trace_id]
                    sp_l1d_reg_mpki[tokens[0]] = result_set[k]['l1d_reg_mpki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l1d_irreg_mpki[tokens[0]] = result_set[k]['l1d_irreg_mpki'] * \
                        simpoints_data[tokens[0]][trace_id]

                    sp_l2c_hits_pki[tokens[0]] = result_set[k]['l2c_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l2c_reg_hits_pki[tokens[0]] = result_set[k]['l2c_reg_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l2c_irreg_hits_pki[tokens[0]] = result_set[k]['l2c_irreg_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l2c_mpki[tokens[0]] = np.array(
                        result_set[k]['l2c_mpki']) * simpoints_data[tokens[0]][trace_id]
                    sp_l2c_reg_mpki[tokens[0]] = result_set[k]['l2c_reg_mpki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l2c_irreg_mpki[tokens[0]] = result_set[k]['l2c_irreg_mpki'] * \
                        simpoints_data[tokens[0]][trace_id]

                    sp_l1d_reg_hit_rate[tokens[0]] = result_set[k]['l1d_reg_hit_rate'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l1d_irreg_hit_rate[tokens[0]] = result_set[k]['l1d_irreg_hit_rate'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l2c_reg_hit_rate[tokens[0]] = result_set[k]['l2c_reg_hit_rate'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l2c_irreg_hit_rate[tokens[0]] = result_set[k]['l2c_irreg_hit_rate'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_reg_hit_rate[tokens[0]] = result_set[k]['llc_reg_hit_rate'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_irreg_hit_rate[tokens[0]] = result_set[k]['llc_irreg_hit_rate'] * \
                        simpoints_data[tokens[0]][trace_id]

                    sp_sdc_hits_pki[tokens[0]] = result_set[k]['sdc_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_sdc_mpki[tokens[0]] = np.array(
                        result_set[k]['sdc_mpki']) * simpoints_data[tokens[0]][trace_id]
                else:
                    sp_l1d_mpki[tokens[0]] = 0.0
                    sp_l1d_reg_mpki[tokens[0]] = 0.0
                    sp_l1d_irreg_mpki[tokens[0]] = 0.0
                    sp_l2c_mpki[tokens[0]] = 0.0
                    sp_llc_sdc_mpki[tokens[0]] = 0.0

                if len(result_set[k]['woc_reuses']) > 0:
                    sp_llc_woc_reuses[tokens[0]] = np.zeros(shape=(1, list(
                        result_set[k]['woc_reuses'].keys())[-1])) * simpoints_data[tokens[0]][trace_id]

                    for k_, v_ in result_set[k]['woc_reuses'].items():
                        sp_llc_woc_reuses[tokens[0]][0, k_ - 1] = v_
                else:
                    sp_llc_woc_reuses[tokens[0]] = np.zeros(shape=(1, 1))

                if is_irreg_stats:
                    sp_llc_reg_misses[tokens[0]] = result_set[k]['reg_misses'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_reg_mpki[tokens[0]] = result_set[k]['reg_mpki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_reg_hits[tokens[0]] = result_set[k]['reg_hits'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_reg_hits_pki[tokens[0]] = result_set[k]['reg_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_irreg_misses[tokens[0]] = result_set[k]['irreg_misses'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_irreg_mpki[tokens[0]] = result_set[k]['irreg_mpki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_irreg_hits[tokens[0]] = result_set[k]['irreg_hits'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_irreg_hits_pki[tokens[0]] = result_set[k]['irreg_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                else:
                    sp_llc_reg_misses[tokens[0]] = 0.0
                    sp_llc_reg_mpki[tokens[0]] = 0.0
                    sp_llc_reg_hits[tokens[0]] = 0.0
                    sp_llc_reg_hits_pki[tokens[0]] = 0.0
                    sp_llc_irreg_misses[tokens[0]] = 0.0
                    sp_llc_irreg_mpki[tokens[0]] = 0.0
                    sp_llc_irreg_hits[tokens[0]] = 0.0
                    sp_llc_irreg_hits_pki[tokens[0]] = 0.0

                sp_served_from[tokens[0]] = {}

                for cache, count in result_set[k]['served_from'].items():
                    sp_served_from[tokens[0]][cache] = count * \
                        simpoints_data[tokens[0]][trace_id]

                sp_re_accuracy[tokens[0]] = {}

                for path, accuracy in result_set[k]['routing_engine']['accuracy'].items():
                    sp_re_accuracy[tokens[0]][path] = accuracy * \
                        simpoints_data[tokens[0]][trace_id]

                sp_ref_front_end_miss_cost[tokens[0]] = ref_set[k]['front_end_miss_cost'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_front_end_miss_cost[tokens[0]] = result_set[k]['front_end_miss_cost'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_irreg_doa[tokens[0]] = result_set[k]['stlb']['irreg_doa'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_irreg_alive[tokens[0]] = result_set[k]['stlb']['irreg_alive'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_reg_doa[tokens[0]] = result_set[k]['stlb']['reg_doa'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_reg_alive[tokens[0]] = result_set[k]['stlb']['reg_alive'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_irreg_accesses_accuracy[tokens[0]] = result_set[k]['irregular_accesses']['accuracy'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_irreg_pred_mpki[tokens[0]] = result_set[k]['irregular_predictor']['mpki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_irreg_pred_change_rate[tokens[0]] = result_set[k]['irregular_predictor']['change_rate'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_energy[tokens[0]] = result_set[k]['energy'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_energy_ref[tokens[0]] = ref_set[k]['energy'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_static_energy[tokens[0]] = result_set[k]['static_energy'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_static_energy_ref[tokens[0]] = ref_set[k]['static_energy'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_dynamic_energy[tokens[0]] = result_set[k]['dynamic_energy'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_dynamic_energy_ref[tokens[0]] = ref_set[k]['dynamic_energy'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_l1d_bypass[tokens[0]] = result_set[k]['l1d_bypass'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l1d_no_bypass[tokens[0]] = result_set[k]['l1d_no_bypass'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_bypass_pred_accuracy[tokens[0]] = result_set[k]['l1d_bypass_predictor']['accuracy'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_metadata_cache_miss_rate[tokens[0]] = result_set[k]['metadata_cache']['miss_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_metadata_cache_mpki[tokens[0]] = result_set[k]['metadata_cache']['mpki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_locmap_accuracy[tokens[0]] = result_set[k]['locmap']['accuracy'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_locmap_outcome[tokens[0]] = result_set[k]['locmap']['outcome_map'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_l1d_offchip_pred_accuracy[tokens[0]] = result_set[k]['l1d_offchip_pred']['accuracy'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_offchip_pred_accuracy[tokens[0]] = (
                    result_set[k]['offchip_pred']['accuracy']) * simpoints_data[tokens[0]][trace_id]
                sp_offchip_pf_pred_accuracy[tokens[0]] = (
                    result_set[k]['offchip_pred']['accuracy_pf']) * simpoints_data[tokens[0]][trace_id]
                sp_offchip_miss_hit_l1d[tokens[0]] = result_set[k]['offchip_pred']['miss_hit_l1d_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_offchip_miss_hit_l2c[tokens[0]] = result_set[k]['offchip_pred']['miss_hit_l2c_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_offchip_miss_hit_l2c_llc[tokens[0]] = result_set[k]['offchip_pred']['miss_hit_l2c_llc_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_offchip_true_pos_rate[tokens[0]] = result_set[k]['offchip_pred']['true_pos_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                # sp_offchip_pf_precision[tokens[0]] = (result_set[k]['offchip_pred']['precision_pf']) * simpoints_data[tokens[0]][trace_id]
                sp_l1d_avoided_hermes[tokens[0]] = result_set[k]['l1d_offchip_pred'][
                    'l1d_saved_hermes_requests'] * simpoints_data[tokens[0]][trace_id]
                sp_l2c_avoided_hermes[tokens[0]] = result_set[k]['l1d_offchip_pred'][
                    'l2c_saved_hermes_requests'] * simpoints_data[tokens[0]][trace_id]
                sp_llc_avoided_hermes[tokens[0]] = result_set[k]['l1d_offchip_pred'][
                    'llc_saved_hermes_requests'] * simpoints_data[tokens[0]][trace_id]

                sp_memory_access_pcs[tokens[0]
                                     ] = result_set[k]['memory_access_pc']
                # sp_hermes_pp_aggreement_rate[tokens[0]] = result_set[k]['offchip_pred_hybrid'][
                #     'aggreement_rate'] * simpoints_data[tokens[0]][trace_id]
                sp_dram_transactions[tokens[0]] = result_set[k]['dram']['transactions'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_stlb_mpki[tokens[0]] = result_set[k]['stlb']['mpki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_doa[tokens[0]] = result_set[k]['stlb']['doa'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_non_doa[tokens[0]] = result_set[k]['stlb']['non_doa'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_l1d_pref_acc[tokens[0]] = result_set[k]['l1d_prefetcher']['accuracy'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l2c_pref_acc[tokens[0]] = result_set[k]['l2c_prefetcher']['accuracy'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_pref_acc[tokens[0]] = result_set[k]['llc_prefetcher']['accuracy'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_l1d_pref_cov[tokens[0]] = result_set[k]['l1d_prefetcher']['coverage'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l2c_pref_cov[tokens[0]] = result_set[k]['l2c_prefetcher']['coverage'] * \
                    simpoints_data[tokens[0]][trace_id]
                
                sp_l1d_pref_misses[tokens[0]] = result_set[k]['l1d_prefetcher']['misses'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l2c_pref_misses[tokens[0]] = result_set[k]['l2c_prefetcher']['misses'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_l1d_pref_issued[tokens[0]] = result_set[k]['l1d_prefetcher']['pf_issued'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l2c_pref_issued[tokens[0]] = result_set[k]['l2c_prefetcher']['pf_issued'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_pref_issued[tokens[0]] = result_set[k]['llc_prefetcher']['pf_issued'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_l1d_useful_pref_loc_l2c[tokens[0]] = result_set[k]['l1d_prefetcher']['useful']['l2c'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l1d_useful_pref_loc_llc[tokens[0]] = result_set[k]['l1d_prefetcher']['useful']['llc'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l1d_useful_pref_loc_dram[tokens[0]] = result_set[k]['l1d_prefetcher']['useful']['dram'] * \
                    simpoints_data[tokens[0]][trace_id]
                
                sp_l1d_useless_pref_loc_l2c[tokens[0]] = result_set[k]['l1d_prefetcher']['useless']['l2c'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l1d_useless_pref_loc_llc[tokens[0]] = result_set[k]['l1d_prefetcher']['useless']['llc'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l1d_useless_pref_loc_dram[tokens[0]] = result_set[k]['l1d_prefetcher']['useless']['dram'] * \
                    simpoints_data[tokens[0]][trace_id]

                # sp_amat[tokens[0]] = result_set[k]['amat'] * simpoints_data[tokens[0]][trace_id]
                # sp_ref_amat[tokens[0]] = ref_set[k]['amat'] * simpoints_data[tokens[0]][trace_id]

                # We also maintain a sum of the weights used as we eventually need to normalize.
                weights_sum[tokens[0]] = simpoints_data[tokens[0]][trace_id]
            else:
                sp_cpi_ref_benches[tokens[0]] += ref_set[k]['cpi'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_cpi_benches[tokens[0]] += result_set[k]['cpi'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_llc_mpki_benches[tokens[0]] += result_set[k]['llc_mpki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_mpki_ref_benches[tokens[0]] += ref_set[k]['llc_mpki'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_llc_loc_hit_pki_benches[tokens[0]] += result_set[k]['llc_loc_hit_pki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_loc_hit_pki_ref[tokens[0]] += ref_set[k]['llc_loc_hit_pki'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_llc_woc_hit_pki_benches[tokens[0]] += result_set[k]['llc_woc_hit_pki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_woc_hit_pki_ref[tokens[0]] += result_set[k]['llc_woc_hit_pki'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_llc_hole_miss_pki_benches[tokens[0]] += result_set[k]['llc_hole_miss_pki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_hole_miss_pki_ref[tokens[0]] += ref_set[k]['llc_hole_miss_pki'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_llc_line_miss_pki_benches[tokens[0]] += result_set[k]['llc_line_miss_pki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_line_miss_pki_ref[tokens[0]] += ref_set[k]['llc_line_miss_pki'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_llc_woc_requested_sizes[tokens[0]] += result_set[k]['requested_sizes'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_sdc_hit_rate[tokens[0]] += result_set[k]['sdc_hit_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l1d_hit_rate[tokens[0]] += result_set[k]['l1d_hit_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l2c_hit_rate[tokens[0]] += result_set[k]['l2c_hit_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_hit_rate[tokens[0]] += result_set[k]['llc_hit_rate'] * \
                    simpoints_data[tokens[0]][trace_id]

                tmp_woc_reuses = None

                if len(result_set[k]['woc_reuses']) > 0:
                    tmp_woc_reuses = np.zeros(shape=(1, list(
                        result_set[k]['woc_reuses'].keys())[-1])) * simpoints_data[tokens[0]][trace_id]

                    for k_, v_ in result_set[k]['woc_reuses'].items():
                        tmp_woc_reuses[0, k_ - 1] = v_
                else:
                    tmp_woc_reuses = np.zeros(shape=(1, 1))

                if sp_llc_woc_reuses[tokens[0]].shape[1] < tmp_woc_reuses.shape[1]:
                    tmp_woc_reuses[0, :sp_llc_woc_reuses[tokens[0]
                                                         ].shape[1]] += sp_llc_woc_reuses[tokens[0]][0, :]

                if is_sdc_stats:
                    sp_l1d_hits_pki[tokens[0]] += result_set[k]['l1d_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l1d_reg_hits_pki[tokens[0]] += result_set[k]['l1d_reg_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l1d_irreg_hits_pki[tokens[0]] += result_set[k]['l1d_irreg_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l1d_mpki[tokens[0]] += np.array(
                        result_set[k]['l1d_mpki']) * simpoints_data[tokens[0]][trace_id]
                    sp_l1d_reg_mpki[tokens[0]] += result_set[k]['l1d_reg_mpki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l1d_irreg_mpki[tokens[0]] += result_set[k]['l1d_irreg_mpki'] * \
                        simpoints_data[tokens[0]][trace_id]

                    sp_l2c_hits_pki[tokens[0]] += result_set[k]['l2c_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l2c_reg_hits_pki[tokens[0]] += result_set[k]['l2c_reg_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l2c_irreg_hits_pki[tokens[0]] += result_set[k]['l2c_irreg_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l2c_mpki[tokens[0]] += np.array(
                        result_set[k]['l2c_mpki']) * simpoints_data[tokens[0]][trace_id]
                    sp_l2c_reg_mpki[tokens[0]] += result_set[k]['l2c_reg_mpki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l2c_irreg_mpki[tokens[0]] += result_set[k]['l2c_irreg_mpki'] * \
                        simpoints_data[tokens[0]][trace_id]

                    sp_l1d_reg_hit_rate[tokens[0]] += result_set[k]['l1d_reg_hit_rate'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l1d_irreg_hit_rate[tokens[0]] += result_set[k]['l1d_irreg_hit_rate'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l2c_reg_hit_rate[tokens[0]] += result_set[k]['l2c_reg_hit_rate'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_l2c_irreg_hit_rate[tokens[0]] += result_set[k]['l2c_irreg_hit_rate'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_reg_hit_rate[tokens[0]] += result_set[k]['llc_reg_hit_rate'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_irreg_hit_rate[tokens[0]] += result_set[k]['llc_irreg_hit_rate'] * \
                        simpoints_data[tokens[0]][trace_id]

                    sp_sdc_hits_pki[tokens[0]] += result_set[k]['sdc_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_sdc_mpki[tokens[0]] += result_set[k]['sdc_mpki'] * \
                        simpoints_data[tokens[0]][trace_id]
                else:
                    sp_l1d_mpki[tokens[0]] += 0.0
                    sp_l1d_reg_mpki[tokens[0]] += 0.0
                    sp_l1d_irreg_mpki[tokens[0]] += 0.0
                    sp_l2c_mpki[tokens[0]] += 0.0
                    sp_llc_sdc_mpki[tokens[0]] += 0.0

                if is_irreg_stats:
                    sp_llc_reg_misses[tokens[0]] += result_set[k]['reg_misses'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_reg_mpki[tokens[0]] += result_set[k]['reg_mpki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_reg_hits[tokens[0]] += result_set[k]['reg_hits'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_reg_hits_pki[tokens[0]] += result_set[k]['reg_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_irreg_misses[tokens[0]] += result_set[k]['irreg_misses'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_irreg_mpki[tokens[0]] += result_set[k]['irreg_mpki'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_irreg_hits[tokens[0]] += result_set[k]['irreg_hits'] * \
                        simpoints_data[tokens[0]][trace_id]
                    sp_llc_irreg_hits_pki[tokens[0]] += result_set[k]['irreg_hits_pki'] * \
                        simpoints_data[tokens[0]][trace_id]
                else:
                    sp_llc_reg_misses[tokens[0]] = 0.0
                    sp_llc_reg_mpki[tokens[0]] = 0.0
                    sp_llc_reg_hits[tokens[0]] = 0.0
                    sp_llc_reg_hits_pki[tokens[0]] = 0.0
                    sp_llc_irreg_misses[tokens[0]] = 0.0
                    sp_llc_irreg_mpki[tokens[0]] = 0.0
                    sp_llc_irreg_hits[tokens[0]] = 0.0
                    sp_llc_irreg_hits_pki[tokens[0]] = 0.0

                for cache, count in result_set[k]['served_from'].items():
                    sp_served_from[tokens[0]][cache] += count * \
                        simpoints_data[tokens[0]][trace_id]

                for path, accuracy in result_set[k]['routing_engine']['accuracy'].items():
                    sp_re_accuracy[tokens[0]][path] += accuracy * \
                        simpoints_data[tokens[0]][trace_id]

                sp_front_end_miss_cost[tokens[0]] += result_set[k]['front_end_miss_cost'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_ref_front_end_miss_cost[tokens[0]] += ref_set[k]['front_end_miss_cost'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_irreg_doa[tokens[0]] += result_set[k]['stlb']['irreg_doa'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_irreg_alive[tokens[0]] += result_set[k]['stlb']['irreg_alive'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_reg_doa[tokens[0]] += result_set[k]['stlb']['reg_doa'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_reg_alive[tokens[0]] += result_set[k]['stlb']['reg_alive'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_irreg_accesses_accuracy[tokens[0]] += result_set[k]['irregular_accesses']['accuracy'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_irreg_pred_mpki[tokens[0]] += result_set[k]['irregular_predictor']['mpki'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_irreg_pred_change_rate[tokens[0]] += result_set[k]['irregular_predictor']['change_rate'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_energy[tokens[0]] += result_set[k]['energy'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_energy_ref[tokens[0]] += ref_set[k]['energy'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_static_energy[tokens[0]] += result_set[k]['static_energy'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_static_energy_ref[tokens[0]] += ref_set[k]['static_energy'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_dynamic_energy[tokens[0]] += result_set[k]['dynamic_energy'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_dynamic_energy_ref[tokens[0]] += ref_set[k]['dynamic_energy'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_l1d_bypass[tokens[0]] += result_set[k]['l1d_bypass'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l1d_no_bypass[tokens[0]] += result_set[k]['l1d_no_bypass'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_bypass_pred_accuracy[tokens[0]] += result_set[k]['l1d_bypass_predictor']['accuracy'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_metadata_cache_miss_rate[tokens[0]] += result_set[k]['metadata_cache']['miss_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_metadata_cache_mpki[tokens[0]] += result_set[k]['metadata_cache']['mpki'] * simpoints_data[tokens[0]][
                    trace_id]
                sp_locmap_accuracy[tokens[0]] += result_set[k]['locmap']['accuracy'] * simpoints_data[tokens[0]][
                    trace_id]
                sp_locmap_outcome[tokens[0]] += result_set[k]['locmap']['outcome_map'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_offchip_pred_accuracy[tokens[0]] += (
                    result_set[k]['offchip_pred']['accuracy']) * simpoints_data[tokens[0]][trace_id]
                sp_offchip_pf_pred_accuracy[tokens[0]] += (
                    result_set[k]['offchip_pred']['accuracy_pf']) * simpoints_data[tokens[0]][trace_id]
                sp_offchip_miss_hit_l1d[tokens[0]] += result_set[k]['offchip_pred']['miss_hit_l1d_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_offchip_miss_hit_l2c[tokens[0]] += result_set[k]['offchip_pred']['miss_hit_l2c_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_offchip_miss_hit_l2c_llc[tokens[0]] += result_set[k]['offchip_pred']['miss_hit_l2c_llc_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_offchip_true_pos_rate[tokens[0]] += result_set[k]['offchip_pred']['true_pos_rate'] * \
                    simpoints_data[tokens[0]][trace_id]
                # sp_offchip_pf_precision[tokens[0]] += (result_set[k]['offchip_pred']['precision_pf']) * simpoints_data[tokens[0]][trace_id]
                sp_l1d_offchip_pred_accuracy[tokens[0]] += result_set[k]['l1d_offchip_pred']['accuracy'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l1d_avoided_hermes[tokens[0]] += result_set[k]['l1d_offchip_pred']['l1d_saved_hermes_requests'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l2c_avoided_hermes[tokens[0]] += result_set[k]['l1d_offchip_pred']['l2c_saved_hermes_requests'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_avoided_hermes[tokens[0]] += result_set[k]['l1d_offchip_pred']['llc_saved_hermes_requests'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_memory_access_pcs[tokens[0]] = max(
                    sp_memory_access_pcs[tokens[0]], result_set[k]['memory_access_pc'])
                # sp_hermes_pp_aggreement_rate[tokens[0]] += result_set[k]['offchip_pred_hybrid']['aggreement_rate'] * \
                #     simpoints_data[tokens[0]][trace_id]
                sp_dram_transactions[tokens[0]] += result_set[k]['dram']['transactions'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_stlb_mpki[tokens[0]] += result_set[k]['stlb']['mpki'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_doa[tokens[0]] += result_set[k]['stlb']['doa'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_non_doa[tokens[0]] += result_set[k]['stlb']['non_doa'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_l1d_pref_acc[tokens[0]] += result_set[k]['l1d_prefetcher']['accuracy'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l2c_pref_acc[tokens[0]] += result_set[k]['l2c_prefetcher']['accuracy'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_pref_acc[tokens[0]] += result_set[k]['llc_prefetcher']['accuracy'] * \
                    simpoints_data[tokens[0]][trace_id]
                
                sp_l1d_pref_cov[tokens[0]] += result_set[k]['l1d_prefetcher']['coverage'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l2c_pref_cov[tokens[0]] += result_set[k]['l2c_prefetcher']['coverage'] * \
                    simpoints_data[tokens[0]][trace_id]
                
                sp_l1d_pref_misses[tokens[0]] += result_set[k]['l1d_prefetcher']['misses'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l2c_pref_misses[tokens[0]] += result_set[k]['l2c_prefetcher']['misses'] * \
                    simpoints_data[tokens[0]][trace_id]


                sp_l1d_pref_issued[tokens[0]] += result_set[k]['l1d_prefetcher']['pf_issued'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l2c_pref_issued[tokens[0]] += result_set[k]['l2c_prefetcher']['pf_issued'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_llc_pref_issued[tokens[0]] += result_set[k]['llc_prefetcher']['pf_issued'] * \
                    simpoints_data[tokens[0]][trace_id]

                sp_l1d_useful_pref_loc_l2c[tokens[0]] += result_set[k]['l1d_prefetcher']['useful']['l2c'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l1d_useful_pref_loc_llc[tokens[0]] += result_set[k]['l1d_prefetcher']['useful']['llc'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l1d_useful_pref_loc_dram[tokens[0]] += result_set[k]['l1d_prefetcher']['useful']['dram'] * \
                    simpoints_data[tokens[0]][trace_id]
                
                sp_l1d_useless_pref_loc_l2c[tokens[0]] += result_set[k]['l1d_prefetcher']['useless']['l2c'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l1d_useless_pref_loc_llc[tokens[0]] += result_set[k]['l1d_prefetcher']['useless']['llc'] * \
                    simpoints_data[tokens[0]][trace_id]
                sp_l1d_useless_pref_loc_dram[tokens[0]] += result_set[k]['l1d_prefetcher']['useless']['dram'] * \
                    simpoints_data[tokens[0]][trace_id]

                # sp_amat[tokens[0]] += result_set[k]['amat'] * simpoints_data[tokens[0]][trace_id]
                # sp_ref_amat[tokens[0]] += ref_set[k]['amat'] * simpoints_data[tokens[0]][trace_id]

                weights_sum[tokens[0]] += simpoints_data[tokens[0]][trace_id]

    # Normalizing per benchmark CPI.
    for k in sp_cpi_ref_benches.keys():
        try:
            sp_cpi_benches[k] /= weights_sum[k]
            sp_cpi_ref_benches[k] /= weights_sum[k]

            sp_llc_mpki_benches[k] /= weights_sum[k]
            sp_llc_mpki_ref_benches[k] /= weights_sum[k]

            sp_llc_loc_hit_pki_benches[k] /= weights_sum[k]
            sp_llc_loc_hit_pki_ref[k] /= weights_sum[k]

            sp_llc_woc_hit_pki_benches[k] /= weights_sum[k]
            sp_llc_woc_hit_pki_ref[k] /= weights_sum[k]

            sp_llc_hole_miss_pki_benches[k] /= weights_sum[k]
            sp_llc_hole_miss_pki_ref[k] /= weights_sum[k]

            sp_llc_line_miss_pki_benches[k] /= weights_sum[k]
            sp_llc_line_miss_pki_ref[k] /= weights_sum[k]

            sp_llc_woc_requested_sizes[k] /= weights_sum[k]

            sp_llc_woc_reuses[k] /= weights_sum[k]

            sp_l1d_hits_pki[k] /= weights_sum[k]
            sp_l1d_reg_hits_pki[k] /= weights_sum[k]
            sp_l1d_irreg_hits_pki[k] /= weights_sum[k]
            sp_l1d_mpki[k] /= weights_sum[k]
            sp_l1d_reg_mpki[k] /= weights_sum[k]
            sp_l1d_irreg_mpki[k] /= weights_sum[k]

            sp_l2c_hits_pki[k] /= weights_sum[k]
            sp_l2c_reg_hits_pki[k] /= weights_sum[k]
            sp_l2c_irreg_hits_pki[k] /= weights_sum[k]
            sp_l2c_mpki[k] /= weights_sum[k]
            sp_l2c_reg_mpki[k] /= weights_sum[k]
            sp_l2c_irreg_mpki[k] /= weights_sum[k]

            sp_l1d_reg_hit_rate[k] /= weights_sum[k]
            sp_l1d_irreg_hit_rate[k] /= weights_sum[k]
            sp_l2c_reg_hit_rate[k] /= weights_sum[k]
            sp_l2c_irreg_hit_rate[k] /= weights_sum[k]
            sp_llc_reg_hit_rate[k] /= weights_sum[k]
            sp_llc_irreg_hit_rate[k] /= weights_sum[k]

            sp_llc_sdc_mpki[k] /= weights_sum[k]

            sp_llc_irreg_misses[k] /= weights_sum[k]
            sp_llc_irreg_mpki[k] /= weights_sum[k]
            sp_llc_irreg_hits[k] /= weights_sum[k]
            sp_llc_irreg_hits_pki[k] /= weights_sum[k]
            sp_llc_reg_misses[k] /= weights_sum[k]
            sp_llc_reg_mpki[k] /= weights_sum[k]
            sp_llc_reg_hits[k] /= weights_sum[k]
            sp_llc_reg_hits_pki[k] /= weights_sum[k]

            sp_sdc_hits_pki[k] /= weights_sum[k]
            sp_sdc_hit_rate[k] /= weights_sum[k]
            sp_l1d_hit_rate[k] /= weights_sum[k]
            sp_l2c_hit_rate[k] /= weights_sum[k]
            sp_llc_hit_rate[k] /= weights_sum[k]

            # Normalizing the counters describing the memory system that served a CPU request.
            for cache in sp_served_from[k].keys():
                sp_served_from[k][cache] /= weights_sum[k]

            # We compute an additional normalization coefficient and we then use it to normalize
            # values of sp_served_from[k] as in some cases no access may be seen.
            norm_coef = reduce(add, sp_served_from[k].values())

            for cache in sp_served_from[k].keys():
                sp_served_from[k][cache] /= norm_coef

            for path in sp_re_accuracy[k].keys():
                sp_re_accuracy[k][path] /= weights_sum[k]

            sp_front_end_miss_cost[k] /= weights_sum[k]
            sp_ref_front_end_miss_cost[k] /= weights_sum[k]

            sp_irreg_doa[k] /= weights_sum[k]
            sp_irreg_alive[k] /= weights_sum[k]
            sp_reg_doa[k] /= weights_sum[k]
            sp_reg_alive[k] /= weights_sum[k]

            sp_irreg_accesses_accuracy[k] /= weights_sum[k]

            sp_irreg_pred_mpki[k] /= weights_sum[k]
            sp_irreg_pred_change_rate[k] /= weights_sum[k]

            sp_energy[k] /= weights_sum[k]
            sp_energy_ref[k] /= weights_sum[k]

            sp_static_energy[k] /= weights_sum[k]
            sp_static_energy_ref[k] /= weights_sum[k]

            sp_dynamic_energy[k] /= weights_sum[k]
            sp_dynamic_energy_ref[k] /= weights_sum[k]

            sp_l1d_bypass[k] /= weights_sum[k]
            sp_l1d_no_bypass[k] /= weights_sum[k]

            sp_bypass_pred_accuracy[k] /= weights_sum[k]

            sp_metadata_cache_miss_rate[k] /= weights_sum[k]
            sp_metadata_cache_mpki[k] /= weights_sum[k]
            sp_locmap_accuracy[k] /= weights_sum[k]
            sp_locmap_outcome[k] /= weights_sum[k]

            sp_offchip_pred_accuracy[k] /= weights_sum[k]
            sp_offchip_pf_pred_accuracy[k] /= weights_sum[k]
            sp_offchip_miss_hit_l1d[k] /= weights_sum[k]
            sp_offchip_miss_hit_l2c[k] /= weights_sum[k]
            sp_offchip_miss_hit_l2c_llc[k] /= weights_sum[k]
            sp_offchip_true_pos_rate[k] /= weights_sum[k]
            # sp_offchip_pf_precision[k] /= weights_sum[k]
            sp_l1d_offchip_pred_accuracy[k] /= weights_sum[k]
            sp_l1d_avoided_hermes[k] /= weights_sum[k]
            sp_l2c_avoided_hermes[k] /= weights_sum[k]
            sp_llc_avoided_hermes[k] /= weights_sum[k]

            sp_stlb_mpki[k] /= weights_sum[k]

            sp_doa[k] /= weights_sum[k]
            sp_non_doa[k] /= weights_sum[k]

            sp_l1d_pref_acc[k] /= weights_sum[k]
            sp_l2c_pref_acc[k] /= weights_sum[k]
            sp_llc_pref_acc[k] /= weights_sum[k]

            sp_l1d_pref_cov[k] /= weights_sum[k]
            sp_l2c_pref_cov[k] /= weights_sum[k]

            sp_l1d_pref_misses[k] /= weights_sum[k]
            sp_l2c_pref_misses[k] /= weights_sum[k]

            sp_l1d_pref_issued[k] /= weights_sum[k]
            sp_l2c_pref_issued[k] /= weights_sum[k]
            sp_llc_pref_issued[k] /= weights_sum[k]

            sp_l1d_useful_pref_loc_l2c[k] /= weights_sum[k]
            sp_l1d_useful_pref_loc_llc[k] /= weights_sum[k]
            sp_l1d_useful_pref_loc_dram[k] /= weights_sum[k]

            sp_l1d_useless_pref_loc_l2c[k] /= weights_sum[k]
            sp_l1d_useless_pref_loc_llc[k] /= weights_sum[k]
            sp_l1d_useless_pref_loc_dram[k] /= weights_sum[k]

            # sp_hermes_pp_aggreement_rate[k] /= weights_sum[k]

            # sp_amat[k] /= weights_sum[k]
            # sp_ref_amat[k] /= weights_sum[k]
        except Exception as exception:
            print(f'excpetion: {k} {result_set.config} {exception}')
        finally:
            pass

    # Now, we will start to fill the result set that serves as an outcome of this function.
    for k in sp_cpi_ref_benches.keys():
        try:
            data_struct = {
                'speedup': sp_cpi_ref_benches[k] / sp_cpi_benches[k],

                'l1d_reg_hit_rate': sp_l1d_reg_hit_rate[k],
                'l1d_irreg_hit_rate': sp_l1d_irreg_hit_rate[k],
                'l2c_reg_hit_rate': sp_l2c_reg_hit_rate[k],
                'l2c_irreg_hit_rate': sp_l2c_irreg_hit_rate[k],
                'llc_reg_hit_rate': sp_llc_reg_hit_rate[k],
                'llc_irreg_hit_rate': sp_llc_irreg_hit_rate[k],

                'reg_misses': sp_llc_reg_misses[k],
                'reg_mpki': sp_llc_reg_mpki[k],
                'reg_hits': sp_llc_reg_hits[k],
                'reg_hits_pki': sp_llc_reg_hits_pki[k],
                'irreg_misses': sp_llc_irreg_misses[k],
                'irreg_mpki': sp_llc_irreg_mpki[k],
                'irreg_hits': sp_llc_irreg_hits[k],
                'irreg_hits_pki': sp_llc_irreg_hits_pki[k],

                'l1d_hits_pki': sp_l1d_hits_pki[k],
                'l1d_reg_hits_pki': sp_l1d_reg_hits_pki[k],
                'l1d_irreg_hits_pki': sp_l1d_irreg_hits_pki[k],
                'l1d_mpki': sp_l1d_mpki[k],
                'l1d_reg_mpki': sp_l1d_reg_mpki[k],
                'l1d_irreg_mpki': sp_l1d_irreg_mpki[k],
                'l1d_bypass_rate': sp_l1d_bypass[k] / (sp_l1d_bypass[k] + sp_l1d_no_bypass[k]) if sp_l1d_bypass[k] + sp_l1d_no_bypass[k] > 0.0 else 0.0,
                'l1d_bypass_predictor': {
                    'accuracy': sp_bypass_pred_accuracy[k],
                },

                'l2c_hits_pki': sp_l2c_hits_pki[k],
                'l2c_reg_hits_pki': sp_l2c_reg_hits_pki[k],
                'l2c_irreg_hits_pki': sp_l2c_irreg_hits_pki[k],
                'l2c_mpki': sp_l2c_mpki[k],
                'l2c_reg_mpki': sp_l2c_reg_mpki[k],
                'l2c_irreg_mpki': sp_l2c_irreg_mpki[k],

                'sdc_mpki': sp_llc_sdc_mpki[k],
                'sdc_hits_pki': sp_sdc_hits_pki[k],

                'sdc_hit_rate': sp_sdc_hit_rate[k],
                'l1d_hit_rate': sp_l1d_hit_rate[k],
                'l2c_hit_rate': sp_l2c_hit_rate[k],
                'llc_hit_rate': sp_llc_hit_rate[k],
                'locality': (1.0 - sp_l1d_hit_rate[k]) * (1.0 - sp_l2c_hit_rate[k]) * (1.0 - sp_llc_hit_rate[k]),

                'llc_mpki': sp_llc_mpki_benches[k],
                'llc_ref_mpki': sp_llc_mpki_ref_benches[k],
                'llc_mpki_reduction': 1.0 - (sp_llc_mpki_benches[k] / sp_llc_mpki_ref_benches[k]) if
                sp_llc_mpki_ref_benches[k] > 0.0 else 0.0,

                'llc_ref_loc_hit_pki': sp_llc_loc_hit_pki_ref[k],
                'llc_ref_line_miss_pki': sp_llc_line_miss_pki_ref[k],

                'llc_loc_hit_pki': sp_llc_loc_hit_pki_benches[k],
                'llc_woc_hit_pki': sp_llc_woc_hit_pki_benches[k],
                'llc_hole_miss_pki': sp_llc_hole_miss_pki_benches[k],
                'llc_line_miss_pki': sp_llc_line_miss_pki_benches[k],

                'llc_distill_cache_mpki_reduction': 1.0 - ((sp_llc_line_miss_pki_benches[k] + sp_llc_hole_miss_pki_benches[k]) / (sp_llc_line_miss_pki_ref[k] + sp_llc_hole_miss_pki_ref[k])),

                # Exposing the requested sizes throughout the workload.
                'requested_sizes': sp_llc_woc_requested_sizes[k],

                'woc_reuses': sp_llc_woc_reuses[k],

                'served_from': sp_served_from[k],
                'routing_engine': {
                    'accuracy': sp_re_accuracy[k]
                },

                'stlb': {
                    'irreg_doa': sp_irreg_doa[k],
                    'irreg_alive': sp_irreg_alive[k],
                    'reg_doa': sp_reg_doa[k],
                    'reg_alive': sp_reg_alive[k],

                    'total_doa': sp_reg_doa[k] + sp_irreg_doa[k],
                    'total_evictions': sp_irreg_doa[k] + sp_irreg_alive[k] + sp_reg_doa[k] + sp_reg_alive[k],

                    'mpki': sp_stlb_mpki[k],

                    'doa': sp_doa[k],
                    'non_doa': sp_non_doa[k],
                },

                'irregular_accesses': {
                    'accuracy': sp_irreg_accesses_accuracy[k],
                },

                'irregular_predictor': {
                    'mpki': sp_irreg_pred_mpki[k],
                    'change_rate': sp_irreg_pred_change_rate[k],
                },

                'metadata_cache': {
                    'miss_rate': sp_metadata_cache_miss_rate[k],
                    'mpki': sp_metadata_cache_mpki[k],
                },

                'locmap': {
                    'accuracy': sp_locmap_accuracy[k],
                    'outcome': sp_locmap_outcome[k],
                },

                'offchip_pred': {
                    'accuracy': sp_offchip_pred_accuracy[k],
                    'accuracy_pf': sp_offchip_pf_pred_accuracy[k],
                    'miss_hit_l1d': sp_offchip_miss_hit_l1d[k],
                    'miss_hit_l2c': sp_offchip_miss_hit_l2c[k],
                    'miss_hit_l2c_llc': sp_offchip_miss_hit_l2c_llc[k],
                    'true_pos_rate': sp_offchip_true_pos_rate[k],
                },

                'l1d_offchip_pred': {
                    'accuracy': sp_l1d_offchip_pred_accuracy[k],
                    # 'hermes_pp_aggreement_rate': sp_hermes_pp_aggreement_rate[k],

                    'l1d_saved_hermes_requests': sp_l1d_avoided_hermes[k],
                    'l2c_saved_hermes_requests': sp_l2c_avoided_hermes[k],
                    'llc_saved_hermes_requests': sp_llc_avoided_hermes[k],
                },

                'dram': {
                    'transactions': sp_dram_transactions[k],
                },

                'memory_access_pcs': sp_memory_access_pcs[k],

                # Exposing the cost of a miss at the front-end of the cache hierarchy.
                'front_end_miss_cost': sp_front_end_miss_cost[k],
                'front_end_miss_cost_reduction': -((sp_front_end_miss_cost[k] / sp_ref_front_end_miss_cost[k]) - 1.0),

                # Energy-related metrics.
                'static_energy': sp_static_energy[k],
                'dynamic_energy': sp_dynamic_energy[k],
                'energy': sp_energy[k],
                'relative_energy': sp_energy[k] / sp_energy_ref[k],

                'l1d_prefetcher': {
                    'issued': sp_l1d_pref_issued[k],
                    'accuracy': sp_l1d_pref_acc[k],
                    'coverage': sp_l1d_pref_cov[k],

                    'misses': sp_l1d_pref_misses[k],

                    'useful': {
                        'l2c': sp_l1d_useful_pref_loc_l2c[k],
                        'llc': sp_l1d_useful_pref_loc_llc[k],
                        'dram': sp_l1d_useful_pref_loc_dram[k],
                    },
                    'useless': {
                        'l2c': sp_l1d_useless_pref_loc_l2c[k],
                        'llc': sp_l1d_useless_pref_loc_llc[k],
                        'dram': sp_l1d_useless_pref_loc_dram[k],
                    },
                },

                'l2c_prefetcher': {
                    'issued': sp_l2c_pref_issued[k],
                    'accuracy': sp_l2c_pref_acc[k],
                    'coverage': sp_l2c_pref_cov[k],

                    'misses': sp_l2c_pref_misses[k],
                },

                'llc_prefetcher': {
                    'issued': sp_llc_pref_issued[k],
                    'accuracy': sp_llc_pref_acc[k]
                },

                # 'amat': sp_amat[k],
                # 'amat_reduction': (sp_amat[k] / sp_ref_amat[k]) - 1.0,

                # Additional information to classify results among benchmark suites.
                'is_spec': re.match(r'^\d{3}[.]{1}', k),
                'is_xsbench': re.match(r'^xs.[X]{1,2}L', k),
                'is_qualcomm': re.match(r'(compute_fp|compute_int|srv|server)', k),
                'is_gapbs': re.match(r'^(sssp|tc|bs|bfs|cc|pr)', k),
            } 

        except Exception as e:
            print(f'exception: {k} {result_set.config} {e}')

            # Putting a placeholder value in order to identify the bugs.
            simpoints_result_set = None
        else:
            # If the usage function didn't reply positively we move on to the next iteration.
            if not use(k, data_struct):
                continue

            simpoints_result_set[k] = data_struct

    # Just a bit of polish, sorting the dictionary by names.
    simpoints_result_set.sort()

    # We also need some data structures to compute the geometric speed-ups.
    elem_speedups = np.zeros(shape=len(simpoints_result_set))
    elem_reg_mpki = np.zeros(shape=len(simpoints_result_set))
    elem_reg_hits_pki = np.zeros(shape=len(simpoints_result_set))
    elem_irreg_mpki = np.zeros(shape=len(simpoints_result_set))
    elem_irreg_hits_pki = np.zeros(shape=len(simpoints_result_set))
    elem_l1d_reg_hit_rate = np.zeros(shape=len(simpoints_result_set))
    elem_l1d_irreg_hit_rate = np.zeros(shape=len(simpoints_result_set))
    elem_l2c_reg_hit_rate = np.zeros(shape=len(simpoints_result_set))
    elem_l2c_irreg_hit_rate = np.zeros(shape=len(simpoints_result_set))
    elem_llc_reg_hit_rate = np.zeros(shape=len(simpoints_result_set))
    elem_llc_irreg_hit_rate = np.zeros(shape=len(simpoints_result_set))
    elem_l1d_hits_pki = np.zeros(shape=len(simpoints_result_set))
    elem_l1d_reg_hits_pki = np.zeros(shape=len(simpoints_result_set))
    elem_l1d_irreg_hits_pki = np.zeros(shape=len(simpoints_result_set))
    elem_l1d_mpki = np.zeros(shape=len(simpoints_result_set))
    elem_l1d_reg_mpki = np.zeros(shape=len(simpoints_result_set))
    elem_l1d_irreg_mpki = np.zeros(shape=len(simpoints_result_set))
    elem_l1d_bypass_rate = np.zeros(shape=len(simpoints_result_set))
    elem_bypass_pred_accuracy = np.zeros(shape=len(simpoints_result_set))
    elem_l2c_hits_pki = np.zeros(shape=len(simpoints_result_set))
    elem_l2c_reg_hits_pki = np.zeros(shape=len(simpoints_result_set))
    elem_l2c_irreg_hits_pki = np.zeros(shape=len(simpoints_result_set))
    elem_l2c_mpki = np.zeros(shape=len(simpoints_result_set))
    elem_l2c_reg_mpki = np.zeros(shape=len(simpoints_result_set))
    eleme_l2c_irreg_mpki = np.zeros(shape=len(simpoints_result_set))
    elem_sdc_mpki = np.zeros(shape=len(simpoints_result_set))
    elem_sdc_hits_pki = np.zeros(shape=len(simpoints_result_set))
    elem_llc_mpkis = np.zeros(shape=len(simpoints_result_set))
    elem_llc_ref_mpkis = np.zeros(shape=len(simpoints_result_set))
    elem_llc_mpki_reductions = np.zeros(shape=len(simpoints_result_set))
    elem_llc_distill_cache_mpki_reduction = np.zeros(
        shape=len(simpoints_result_set))
    elem_front_end_miss_cost = np.zeros(shape=len(simpoints_result_set))
    elem_front_end_miss_cost_reduction = np.zeros(
        shape=len(simpoints_result_set))
    # elem_amat = np.zeros(shape=len(simpoints_result_set))
    # elem_amat_reduction = np.zeros(shape=len(simpoints_result_set))
    elem_sdc_hit_rate = np.zeros(shape=len(simpoints_result_set))
    elem_l1d_hit_rate = np.zeros(shape=len(simpoints_result_set))
    elem_l2c_hit_rate = np.zeros(shape=len(simpoints_result_set))
    elem_llc_hit_rate = np.zeros(shape=len(simpoints_result_set))
    elem_locality = np.zeros(shape=len(simpoints_result_set))

    elem_re_global_accuracy = np.zeros(shape=len(simpoints_result_set))
    elem_re_l2c_accuracy = np.zeros(shape=len(simpoints_result_set))
    elem_re_llc_accuracy = np.zeros(shape=len(simpoints_result_set))
    elem_re_dram_accuracy = np.zeros(shape=len(simpoints_result_set))

    elem_irregular_accesses_accuracy = np.zeros(
        shape=len(simpoints_result_set))

    elem_irregular_pred_mpki = np.zeros(shape=len(simpoints_result_set))
    elem_irregular_pred_change_rate = np.zeros(shape=len(simpoints_result_set))

    elem_irreg_doa = np.zeros(shape=len(simpoints_result_set))
    elem_irreg_alive = np.zeros(shape=len(simpoints_result_set))
    elem_reg_doa = np.zeros(shape=len(simpoints_result_set))
    elem_reg_alive = np.zeros(shape=len(simpoints_result_set))

    elem_static_energy = np.zeros(shape=len(simpoints_result_set))
    elem_dynamic_energy = np.zeros(shape=len(simpoints_result_set))
    elem_energy = np.zeros(shape=len(simpoints_result_set))
    elem_relative_energy = np.zeros(shape=len(simpoints_result_set))
    elem_metadata_cache_miss_rate = np.zeros(shape=len(simpoints_result_set))
    elem_metadata_cache_mpki = np.zeros(shape=len(simpoints_result_set))
    elem_locmap_accuracy = np.zeros(shape=len(simpoints_result_set))

    elem_memory_access_pcs = np.zeros(shape=len(simpoints_result_set))

    elem_offchip_pred_accuracy = np.zeros(shape=len(simpoints_result_set))
    elem_offchip_pf_pred_accuracy = np.zeros(shape=len(simpoints_result_set))
    elem_l1d_offchip_pred_accuracy = np.zeros(shape=len(simpoints_result_set))
    elem_offchip_miss_hit_l1d = np.zeros(shape=len(simpoints_result_set))
    elem_offchip_miss_hit_l2c = np.zeros(shape=len(simpoints_result_set))
    elem_offchip_miss_hit_l2c_llc = np.zeros(shape=len(simpoints_result_set))
    elem_offchip_true_pos_rate = np.zeros(shape=len(simpoints_result_set))
    elem_l1d_avoided_hermes = np.zeros(shape=len(simpoints_result_set))
    elem_l2c_avoided_hermes = np.zeros(shape=len(simpoints_result_set))
    elem_llc_avoided_hermes = np.zeros(shape=len(simpoints_result_set))

    elem_hermes_pp_aggreement_rate = np.zeros(shape=len(simpoints_result_set))
    elem_dram_transactions = np.zeros(shape=len(simpoints_result_set))

    elem_stlb_mpki = np.zeros(shape=len(simpoints_result_set))
    elem_doa = np.zeros(shape=len(simpoints_result_set))
    elem_non_doa = np.zeros(shape=len(simpoints_result_set))

    elem_l1d_pref_acc = np.zeros(shape=len(simpoints_result_set))
    elem_l2c_pref_acc = np.zeros(shape=len(simpoints_result_set))
    elem_llc_pref_acc = np.zeros(shape=len(simpoints_result_set))

    elem_l1d_pref_cov = np.zeros(shape=len(simpoints_result_set))
    elem_l2c_pref_cov = np.zeros(shape=len(simpoints_result_set))

    elem_l1d_pref_misses = np.zeros(shape=len(simpoints_result_set))
    elem_l2c_pref_misses = np.zeros(shape=len(simpoints_result_set))

    elem_l1d_pref_issued = np.zeros(shape=len(simpoints_result_set))
    elem_l2c_pref_issued = np.zeros(shape=len(simpoints_result_set))
    elem_llc_pref_issued = np.zeros(shape=len(simpoints_result_set))

    elem_l1d_useful_loc_l2c, elem_l1d_useful_loc_llc, elem_l1d_useful_loc_dram = np.zeros(shape=len(
        simpoints_result_set)), np.zeros(shape=len(simpoints_result_set)), np.zeros(shape=len(simpoints_result_set))
    elem_l1d_useless_loc_l2c, elem_l1d_useless_loc_llc, elem_l1d_useless_loc_dram = np.zeros(shape=len(
        simpoints_result_set)), np.zeros(shape=len(simpoints_result_set)), np.zeros(shape=len(simpoints_result_set))

    # At last, we can compute geometric mean speed-up for this configuration.
    for idx, key in enumerate(simpoints_result_set.keys()):
        elem_speedups[idx] = simpoints_result_set[key]['speedup']
        elem_reg_mpki[idx] = simpoints_result_set[key]['reg_mpki']
        elem_reg_hits_pki[idx] = simpoints_result_set[key]['reg_hits_pki']
        elem_irreg_mpki[idx] = simpoints_result_set[key]['irreg_mpki']
        elem_irreg_hits_pki[idx] = simpoints_result_set[key]['irreg_hits_pki']
        elem_l1d_reg_hit_rate[idx] = simpoints_result_set[key]['l1d_reg_hit_rate']
        elem_l1d_irreg_hit_rate[idx] = simpoints_result_set[key]['l1d_irreg_hit_rate']
        elem_l2c_reg_hit_rate[idx] = simpoints_result_set[key]['l2c_reg_hit_rate']
        elem_l2c_irreg_hit_rate[idx] = simpoints_result_set[key]['l2c_irreg_hit_rate']
        elem_llc_reg_hit_rate[idx] = simpoints_result_set[key]['llc_reg_hit_rate']
        elem_llc_irreg_hit_rate[idx] = simpoints_result_set[key]['llc_irreg_hit_rate']
        elem_l1d_hits_pki[idx] = simpoints_result_set[key]['l1d_hits_pki']
        elem_l1d_reg_hits_pki[idx] = simpoints_result_set[key]['l1d_reg_hits_pki']
        elem_l1d_irreg_hits_pki[idx] = simpoints_result_set[key]['l1d_irreg_hits_pki']
        elem_l1d_mpki[idx] = simpoints_result_set[key]['l1d_mpki']
        elem_l1d_reg_mpki[idx] = simpoints_result_set[key]['l1d_reg_mpki']
        elem_l1d_irreg_mpki[idx] = simpoints_result_set[key]['l1d_irreg_mpki']
        elem_l1d_bypass_rate[idx] = simpoints_result_set[key]['l1d_bypass_rate']
        elem_bypass_pred_accuracy[idx] = simpoints_result_set[key]['l1d_bypass_predictor']['accuracy']
        elem_l2c_hits_pki[idx] = simpoints_result_set[key]['l2c_hits_pki']
        elem_l2c_reg_hits_pki[idx] = simpoints_result_set[key]['l2c_reg_hits_pki']
        elem_l2c_irreg_hits_pki[idx] = simpoints_result_set[key]['l2c_irreg_hits_pki']
        elem_l2c_mpki[idx] = simpoints_result_set[key]['l2c_mpki']
        elem_l2c_reg_mpki[idx] = simpoints_result_set[key]['l2c_reg_mpki']
        eleme_l2c_irreg_mpki[idx] = simpoints_result_set[key]['l2c_irreg_mpki']
        elem_sdc_hits_pki[idx] = simpoints_result_set[key]['sdc_hits_pki']
        elem_sdc_mpki[idx] = simpoints_result_set[key]['sdc_mpki']
        elem_llc_mpkis[idx] = simpoints_result_set[key]['llc_mpki']
        elem_llc_ref_mpkis[idx] = simpoints_result_set[key]['llc_ref_mpki']
        elem_llc_mpki_reductions[idx] = simpoints_result_set[key]['llc_mpki_reduction']
        elem_llc_distill_cache_mpki_reduction[idx] = simpoints_result_set[key]['llc_distill_cache_mpki_reduction']
        elem_front_end_miss_cost[idx] = simpoints_result_set[key]['front_end_miss_cost']
        elem_front_end_miss_cost_reduction[idx] = simpoints_result_set[key]['front_end_miss_cost_reduction']
        # elem_amat[idx] = simpoints_result_set[key]['amat']
        # elem_amat_reduction[idx] = simpoints_result_set[key]['amat_reduction']
        elem_sdc_hit_rate[idx] = simpoints_result_set[key]['sdc_hit_rate']
        elem_l1d_hit_rate[idx] = simpoints_result_set[key]['l1d_hit_rate']
        elem_l2c_hit_rate[idx] = simpoints_result_set[key]['l2c_hit_rate']
        elem_llc_hit_rate[idx] = simpoints_result_set[key]['llc_hit_rate']
        elem_locality[idx] = simpoints_result_set[key]['locality']

        elem_re_global_accuracy[idx] = simpoints_result_set[key]['routing_engine']['accuracy']['global']
        elem_re_l2c_accuracy[idx] = simpoints_result_set[key]['routing_engine']['accuracy']['l2c']
        elem_re_llc_accuracy[idx] = simpoints_result_set[key]['routing_engine']['accuracy']['llc']
        elem_re_dram_accuracy[idx] = simpoints_result_set[key]['routing_engine']['accuracy']['dram']

        elem_irregular_accesses_accuracy[idx] = simpoints_result_set[key]['irregular_accesses']['accuracy']

        elem_irregular_pred_mpki[idx] = simpoints_result_set[key]['irregular_predictor']['mpki']
        elem_irregular_pred_change_rate[idx] = simpoints_result_set[key]['irregular_predictor']['change_rate']

        elem_irreg_doa[idx] = simpoints_result_set[key]['stlb']['irreg_doa']
        elem_irreg_alive[idx] = simpoints_result_set[key]['stlb']['irreg_alive']
        elem_reg_doa[idx] = simpoints_result_set[key]['stlb']['reg_doa']
        elem_reg_alive[idx] = simpoints_result_set[key]['stlb']['reg_alive']

        elem_static_energy[idx] = simpoints_result_set[key]['static_energy']
        elem_dynamic_energy[idx] = simpoints_result_set[key]['dynamic_energy']
        elem_energy[idx] = simpoints_result_set[key]['energy']
        elem_relative_energy[idx] = simpoints_result_set[key]['relative_energy']

        elem_metadata_cache_miss_rate[idx] = simpoints_result_set[key]['metadata_cache']['miss_rate']
        elem_metadata_cache_mpki[idx] = simpoints_result_set[key]['metadata_cache']['mpki']
        elem_locmap_accuracy[idx] = simpoints_result_set[key]['locmap']['accuracy']

        elem_memory_access_pcs[idx] = simpoints_result_set[key]['memory_access_pcs']
        elem_offchip_pred_accuracy[idx] = simpoints_result_set[key]['offchip_pred']['accuracy']
        elem_offchip_pf_pred_accuracy[idx] = simpoints_result_set[key]['offchip_pred']['accuracy_pf']
        elem_offchip_miss_hit_l1d[idx] = simpoints_result_set[key]['offchip_pred']['miss_hit_l1d']
        elem_offchip_miss_hit_l2c[idx] = simpoints_result_set[key]['offchip_pred']['miss_hit_l2c']
        elem_offchip_miss_hit_l2c_llc[idx] = simpoints_result_set[key]['offchip_pred']['miss_hit_l2c_llc']
        elem_offchip_true_pos_rate[idx] = simpoints_result_set[key]['offchip_pred']['true_pos_rate']
        elem_l1d_offchip_pred_accuracy[idx] = simpoints_result_set[key]['l1d_offchip_pred']['accuracy']
        elem_l1d_avoided_hermes[idx] = simpoints_result_set[key]['l1d_offchip_pred']['l1d_saved_hermes_requests']
        elem_l2c_avoided_hermes[idx] = simpoints_result_set[key]['l1d_offchip_pred']['l2c_saved_hermes_requests']
        elem_llc_avoided_hermes[idx] = simpoints_result_set[key]['l1d_offchip_pred']['llc_saved_hermes_requests']

        # elem_hermes_pp_aggreement_rate[idx] = simpoints_result_set[key]['l1d_offchip_pred']['hermes_pp_aggreement_rate']

        elem_dram_transactions[idx] = simpoints_result_set[key]['dram']['transactions']

        elem_stlb_mpki[idx] = simpoints_result_set[key]['stlb']['mpki']

        elem_doa[idx] = simpoints_result_set[key]['stlb']['doa']
        elem_non_doa[idx] = simpoints_result_set[key]['stlb']['non_doa']

        elem_l1d_pref_acc[idx] = simpoints_result_set[key]['l1d_prefetcher']['accuracy']
        elem_l2c_pref_acc[idx] = simpoints_result_set[key]['l2c_prefetcher']['accuracy']
        elem_llc_pref_acc[idx] = simpoints_result_set[key]['llc_prefetcher']['accuracy']

        elem_l1d_pref_cov[idx] = simpoints_result_set[key]['l1d_prefetcher']['coverage']
        elem_l2c_pref_cov[idx] = simpoints_result_set[key]['l2c_prefetcher']['coverage']

        elem_l1d_pref_misses[idx] = simpoints_result_set[key]['l1d_prefetcher']['misses']
        elem_l2c_pref_misses[idx] = simpoints_result_set[key]['l2c_prefetcher']['misses']

        elem_l1d_pref_issued[idx] = simpoints_result_set[key]['l1d_prefetcher']['issued']
        elem_l2c_pref_issued[idx] = simpoints_result_set[key]['l2c_prefetcher']['issued']
        elem_llc_pref_issued[idx] = simpoints_result_set[key]['llc_prefetcher']['issued']

        elem_l1d_useful_loc_l2c[idx] = simpoints_result_set[key]['l1d_prefetcher']['useful']['l2c']
        elem_l1d_useful_loc_llc[idx] = simpoints_result_set[key]['l1d_prefetcher']['useful']['llc']
        elem_l1d_useful_loc_dram[idx] = simpoints_result_set[key]['l1d_prefetcher']['useful']['dram']

        elem_l1d_useless_loc_l2c[idx] = simpoints_result_set[key]['l1d_prefetcher']['useless']['l2c']
        elem_l1d_useless_loc_llc[idx] = simpoints_result_set[key]['l1d_prefetcher']['useless']['llc']
        elem_l1d_useless_loc_dram[idx] = simpoints_result_set[key]['l1d_prefetcher']['useless']['dram']

    simpoints_result_set['geomean'] = {
        'speedup': gmean(elem_speedups),
    }

    simpoints_result_set['mean'] = {
        'reg_mpki': np.average(elem_reg_mpki),
        'reg_hits_pki': np.average(elem_reg_hits_pki),
        'irreg_mpki': np.average(elem_irreg_mpki),
        'irreg_hits_pki': np.average(elem_irreg_hits_pki),
        'l1d_reg_hit_rate': np.average(elem_l1d_reg_hit_rate),
        'l1d_irreg_hit_rate': np.average(elem_l1d_irreg_hit_rate),
        'l1d_bypass_rate': np.average(elem_l1d_bypass_rate),
        'l2c_reg_hit_rate': np.average(elem_l2c_reg_hit_rate),
        'l2c_irreg_hit_rate': np.average(elem_l2c_irreg_hit_rate),
        'llc_reg_hit_rate': np.average(elem_llc_reg_hit_rate),
        'llc_irreg_hit_rate': np.average(elem_llc_irreg_hit_rate),
        'l1d_hits_pki': np.average(elem_l1d_hits_pki),
        'l1d_reg_hits_pki': np.average(elem_l1d_reg_hits_pki),
        'l1d_irreg_hits_pki': np.average(elem_l1d_irreg_hits_pki),
        'l1d_mpki': np.average(elem_l1d_mpki),
        'l1d_reg_mpki': np.average(elem_l1d_reg_mpki),
        'l1d_irreg_mpki': np.average(elem_l1d_irreg_mpki),

        'l1d_bypass_predictor': {
            'accuracy': np.average(elem_bypass_pred_accuracy),
        },

        'l2c_hits_pki': np.average(elem_l2c_hits_pki),
        'l2c_reg_hits_pki': np.average(elem_l2c_reg_hits_pki),
        'l2c_irreg_hits_pki': np.average(elem_l2c_irreg_hits_pki),
        'l2c_mpki': np.average(elem_l2c_mpki),
        'l2c_reg_mpki': np.average(elem_l2c_reg_mpki),
        'l2c_irreg_mpki': np.average(elem_l2c_irreg_hits_pki),

        'sdc_hit_rate': np.average(elem_sdc_hit_rate),
        'l1d_hit_rate': np.average(elem_l1d_hit_rate),
        'l2c_hit_rate': np.average(elem_l2c_hit_rate),
        'llc_hit_rate': np.average(elem_llc_hit_rate),
        'locality': np.average(elem_locality),

        'sdc_hits_pki': np.average(elem_sdc_hits_pki),
        'sdc_mpki': np.average(elem_sdc_mpki),
        'llc_mpki': np.average(elem_llc_mpkis),
        'llc_ref_mpki': np.average(elem_llc_ref_mpkis),
        'llc_mpki_reduction': np.average(elem_llc_mpki_reductions),
        'llc_distill_cache_mpki_reduction': np.average(elem_llc_distill_cache_mpki_reduction),

        'front_end_miss_cost': np.average(elem_front_end_miss_cost),
        'front_end_miss_cost_reduction': np.average(elem_front_end_miss_cost_reduction),

        # 'amat': np.average(elem_amat),
        # 'amat_reduction': np.average(elem_amat_reduction),

        'routing_engine': {
            'accuracy': {
                'global': np.average(elem_re_global_accuracy),
                'l2c': np.average(elem_re_l2c_accuracy),
                'llc': np.average(elem_re_llc_accuracy),
                'dram': np.average(elem_re_dram_accuracy),
            }
        },

        'stlb': {
            'irreg_doa': np.average(elem_irreg_doa),
            'irreg_alive': np.average(elem_irreg_alive),
            'reg_doa': np.average(elem_reg_doa),
            'reg_alive': np.average(elem_reg_alive),

            'mpki': np.average(elem_stlb_mpki),

            'doa': np.average(elem_doa),
            'non_doa': np.average(elem_non_doa),
        },

        'irregular_accesses': {
            'accuracy': np.average(elem_irregular_accesses_accuracy)
        },

        'irregular_predictor': {
            'mpki': np.average(elem_irregular_pred_mpki),
            'change_rate': np.average(elem_irregular_pred_change_rate),
        },

        'metadata_cache': {
            'miss_rate': np.average(elem_metadata_cache_miss_rate),
            'mpki': np.average(elem_metadata_cache_mpki),
        },

        'locmap': {
            'accuracy': np.average(elem_locmap_accuracy),
        },

        'offchip_pred': {
            'accuracy': np.average(elem_offchip_pred_accuracy),
            'accuracy_pf': np.average(elem_offchip_pf_pred_accuracy),
            'miss_hit_l1d': np.average(elem_offchip_miss_hit_l1d),
            'miss_hit_l2c': np.average(elem_offchip_miss_hit_l2c),
            'miss_hit_l2c_llc': np.average(elem_offchip_miss_hit_l2c_llc),
            'true_pos_rate': np.average(elem_offchip_true_pos_rate),
        },

        'l1d_offchip_pred': {
            'accuracy': np.average(elem_l1d_offchip_pred_accuracy),
            # 'hermes_pp_aggreement_rate': np.average(elem_hermes_pp_aggreement_rate),

            'l1d_saved_hermes_requests': np.average(elem_l1d_avoided_hermes),
            'l2c_saved_hermes_requests': np.average(elem_l2c_avoided_hermes),
            'llc_saved_hermes_requests': np.average(elem_llc_avoided_hermes),
        },

        'dram': {
            'transactions': np.average(elem_dram_transactions),
        },

        'memory_access_pcs': np.average(elem_memory_access_pcs),

        'static_energy': np.average(elem_static_energy),
        'dynamic_energy': np.average(elem_dynamic_energy),
        'energy': np.average(elem_energy),
        'relative_energy': np.average(elem_relative_energy),

        'l1d_prefetcher': {
            'issued': np.average(elem_l1d_pref_issued),
            'accuracy': np.average(elem_l1d_pref_acc),
            'coverage': np.average(elem_l1d_pref_cov),
            'misses': np.average(elem_l1d_pref_misses),

            'useful': {
                'l2c': np.average(elem_l1d_useful_loc_l2c),
                'llc': np.average(elem_l1d_useful_loc_llc),
                'dram': np.average(elem_l1d_useful_loc_dram),
            },
            'useless': {
                'l2c': np.average(elem_l1d_useless_loc_l2c),
                'llc': np.average(elem_l1d_useless_loc_llc),
                'dram': np.average(elem_l1d_useless_loc_dram),
            },
        },

        'l2c_prefetcher': {
            'issued': np.average(elem_l2c_pref_issued),
            'accuracy': np.average(elem_l2c_pref_acc),
            'coverage': np.average(elem_l2c_pref_cov),

            'misses': np.average(elem_l2c_pref_misses),
        },

        'llc_prefetcher': {
            'issued': np.average(elem_llc_pref_issued),
            'accuracy': np.average(elem_llc_pref_acc),
        },
    }

    simpoints_result_set['mean']['stlb']['total_doa'] = simpoints_result_set['mean']['stlb']['irreg_doa'] + \
        simpoints_result_set['mean']['stlb']['reg_doa']
    simpoints_result_set['mean']['stlb']['total_evictions'] = np.average(elem_irreg_alive) + np.average(elem_irreg_doa) + \
        np.average(elem_reg_doa) + np.average(elem_reg_alive)

    return simpoints_result_set


def apply_block_usage_simpoint(results_set, simpoint_data, use=lambda n, d: True):
    simpoints_results_set = ResultSet(results_set.config)

    # To go through all these computation, we will need a couple of helper structures.
    sp_l1i_usage_benches, sp_l1d_usage_benches, sp_l2c_usage_benches,\
        sp_llc_usage_benches, sp_avg_entropy_benches = \
        {}, {}, {}, {}, {}
    weights_sum = {}

    # Now, let's get into the real mechanic of this computation.
    for k in results_set.keys():
        # Sanity checks and pre-processing.
        results_set[k]['l1i_block_usage'].pop(0, None)
        results_set[k]['l1d_block_usage'].pop(0, None)
        results_set[k]['l2c_block_usage'].pop(0, None)
        results_set[k]['llc_block_usage'].pop(0, None)

        tokens, trace_name, trace_id = k.split('-'), str(), str()

        # Getting the trace name.
        trace_name = tokens[0]
        trace_id = tokens[1].split('.')[0][:-1]

        if trace_name not in sp_l1i_usage_benches:
            # Putting data in the dictionnaries.
            sp_l1i_usage_benches[trace_name] = np.array(list(
                results_set[k]['l1i_block_usage'].values())) * simpoint_data[trace_name][trace_id]
            sp_l1d_usage_benches[trace_name] = np.array(list(
                results_set[k]['l1d_block_usage'].values())) * simpoint_data[trace_name][trace_id]
            sp_l2c_usage_benches[trace_name] = np.array(list(
                results_set[k]['l2c_block_usage'].values())) * simpoint_data[trace_name][trace_id]
            sp_llc_usage_benches[trace_name] = np.array(list(
                results_set[k]['llc_block_usage'].values())) * simpoint_data[trace_name][trace_id]
            sp_avg_entropy_benches[trace_name] = results_set[k]['avg_entropy'] * \
                simpoint_data[trace_name][trace_id]

            # Accumulating weigts.
            weights_sum[trace_name] = simpoint_data[trace_name][trace_id]
        else:
            sp_l1i_usage_benches[trace_name] = np.add(sp_l1i_usage_benches[trace_name], np.array(
                list(results_set[k]['l1i_block_usage'].values())) * simpoint_data[trace_name][trace_id])
            sp_l1d_usage_benches[trace_name] = np.add(sp_l1i_usage_benches[trace_name], np.array(
                list(results_set[k]['l1d_block_usage'].values())) * simpoint_data[trace_name][trace_id])
            sp_l2c_usage_benches[trace_name] = np.add(sp_l1i_usage_benches[trace_name], np.array(
                list(results_set[k]['l2c_block_usage'].values())) * simpoint_data[trace_name][trace_id])
            sp_llc_usage_benches[trace_name] = np.add(sp_l1i_usage_benches[trace_name], np.array(
                list(results_set[k]['llc_block_usage'].values())) * simpoint_data[trace_name][trace_id])
            sp_avg_entropy_benches[trace_name] += results_set[k]['avg_entropy'] * \
                simpoint_data[trace_name][trace_id]

            weights_sum[trace_name] += simpoint_data[trace_name][trace_id]

    # Normalizing per benchmarks the obtained metrics.
    for k in sp_l1i_usage_benches.keys():
        sp_l1i_usage_benches[k] /= weights_sum[k]
        sp_l1d_usage_benches[k] /= weights_sum[k]
        sp_l2c_usage_benches[k] /= weights_sum[k]
        sp_llc_usage_benches[k] /= weights_sum[k]
        sp_avg_entropy_benches[k] /= weights_sum[k]

    for k in sp_l1i_usage_benches.keys():
        data_struct = {
            'l1i_block_usage': sp_l1i_usage_benches[k],
            'l1d_block_usage': sp_l1d_usage_benches[k],
            'l2c_block_usage': sp_l2c_usage_benches[k],
            'llc_block_usage': sp_llc_usage_benches[k],
            'avg_entropy': sp_avg_entropy_benches[k]
        }

        if not use(k, data_struct):
            continue

        simpoints_results_set[k] = data_struct

    simpoints_results_set.sort()

    return simpoints_results_set


def pairing_multicore_result_sets(exp, baseline_name, verbose=False):
    """
    The purpose of this function is to pair result sets based on a given design. Basically
    a pair would be made of an execution using the multi-core context and another one using
    the single-core context.

    :param exp: The experiment sets in which to look for pairs.
    :return:
    """
    re_design = re.compile(r'(.*)_(?:mixes|singles)$')
    avail_conf = [e.config['bin'] for e in exp.sets]
    diff_designs = set()
    pairs = []
    baseline_pair = ()

    for conf in avail_conf:
        # Checking for the design name and adding it to list if not there yet.
        re_res = re_design.search(conf)

        if re_res is None:
            raise Exception(f'{conf} is not matching naming conventions.')

        # As diff_designs is a set we won't face duplicates.
        diff_designs.add(re_res.group(1))

    # Now we can do the actual pairing.
    for design in diff_designs:
        mixed, single = None, None

        # For each design, we look for both the mixed execution and the single one.
        for e in exp.sets:
            if e.config['bin'] == f'{design}_mixes':
                mixed = e
            elif e.config['bin'] == f'{design}_singles':
                single = e

        # Sanity check.
        if mixed is None or single is None:
            raise Exception(
                f'We haven\'t been able to build a pair for {design}')

        # We add the pair to the list and reset helpers.
        pairs += [(mixed, single)]

        # If that the designated baseline we mark it.
        if design == baseline_name:
            baseline_pair = pairs[-1]

        # Let's output some information so people are aware of what is going on here.
        if verbose:
            print(
                f'[INFO] Paired {mixed.config["bin"]} with {single.config["bin"]}')

    return pairs, baseline_pair


def compute_multicore_weighted_ipc(pair, mixes, verbose=False):
    """
    The purpose of that function is to compute the weighted IPC of a multi-core execution based on both the
    IPCs of each core while running in the multi-core context and the IPCs of each individual core running
    in isolation.
    :param pair: A pair of result sets for a given design. The first element being the results in the multi-core
    context, the second being the results in the single-core (isolation) context.
    :param mixes: A dictionary containing details about each mixes simulated.
    :param verbose: Should we print some more details about what is going on here?
    :return:
    """
    mixed, single = pair[0], pair[1]

    config = pair[0].config

    # For each mix we compute its weighted IPC and update the matching data structure.
    for mix, mix_data in mixed.items():
        curr_mix_components = mixes[mix]

        # For each component of a mix we fill two nparrays with the appropriate data.
        shared_ipcs, single_cpis, single_llc_mpki = np.zeros(
            shape=len(curr_mix_components)), np.zeros(shape=len(curr_mix_components)), np.zeros(shape=len(curr_mix_components))

        try:
            for idx, curr_component in enumerate(curr_mix_components):
                shared_ipcs[idx] = mix_data['ipcs'][idx]

                # As the inverse of the IPC is used as a weight, we can simply grab the CPI and use it.
                single_cpis[idx] = single[f'{curr_component}.txt']['cpi']
                single_llc_mpki[idx] = single[f'{curr_component}.txt']['llc_mpki']
        except Exception as err:
            # Upon an expcetion, we simply jump to the nex titeration.
            continue

        # Now that we have the elements required for computation, we simply computed the weighted speed-up.
        w_ipc = np.average(shared_ipcs, axis=None, weights=single_cpis)
        w_llc_mpki = np.average(single_llc_mpki, axis=None, weights=single_cpis)

        mix_data['weighted_ipc'] = w_ipc
        mix_data['weighted_llc_mpki'] = w_llc_mpki

        mix_data['energy'] = [0.0 for _ in range(len(mix_data['ipcs']))]
        low_ipc = min(shared_ipcs)

        # WIP: Here, we compute the energy consumed by the chip running a mix of traces.
        for cache_name, cache in mix_data['caches'].items():
            energy_model = mix_data['energy_models'][cache_name]

            for cpu_id, cache_data in enumerate(cache):
                # Building a dictionnary containing the relevant parameters for energy computation.
                param_dict = {
                    'loads': cache_data['loads'],
                    'stores': cache_data['stores'],
                    'time': (50 * (10 ** 6)) / (3.8 * (10 ** 9)),
                }

                if cache_name:
                    param_dict['time'] *= (1.0 / low_ipc)
                else:
                    param_dict['time'] *= (1.0 / shared_ipcs[cpu_id])

                cache_energy = energy_model.compute(**param_dict)

                mix_data['energy'][cpu_id] += reduce(add,
                                                     cache_energy.values())

        mix_data['total_energy'] = reduce(add, mix_data['energy'])

        # WIP: Here, we compute a metrics relative to the DRAM transactions.
        mix_data['dram']['dpc'] = mix_data['dram']['transactions']
        # mix_data['dram']['dpc'] = mix_data['dram']['transactions'] / (config['simulation_instructions'] / low_ipc)


def compute_multicore_speedup(design, baseline):
    keys_to_remove = []

    for mix, mix_data in design.items():
        if mix not in baseline:
            keys_to_remove += [mix]
            continue

        try:
            mix_data['speedup'] = mix_data['weighted_ipc'] / \
                baseline[mix]['weighted_ipc']

            # WIP: Alongside the speed-up, we compute the relative number of DRAM transactions.
            # mix_data['dram']['relative_transactions'] = mix_data['dram']['transactions'] / baseline[mix]['dram']['transactions']
            mix_data['dram']['relative_dpc'] = mix_data['dram']['dpc'] / \
                baseline[mix]['dram']['dpc']

            mix_data['relative_total_energy'] = mix_data['total_energy'] / \
                baseline[mix]['total_energy']
        except Exception as err:
            mix_data['speedup'] = 1.0
            mix_data['dram']['relative_dpc'] = 1.0
            mix_data['relative_total_energy'] = 1.0
            continue
    # Cleaning up the result set of the mixes for which the speed-up was not computable.
    for e in keys_to_remove:
        del design[e]

    # Here's a beautify section. We can sort mixes by ascending order of speed-up.
    design._results = OrderedDict(
        sorted(design.items(), key=lambda e: e[1]['speedup']))

    # Also computing the geometric mean speedup.
    gm_sp = scipy.stats.gmean([v['speedup'] for v in design.values()])

    design['geomean'] = {'speedup': gm_sp}

    # WIP: Computing the average relative number of DRAM transaction.
    design['mean'] = {
        'dram': {'relative_dpc': np.average([v['dram']['relative_dpc'] for k, v in design.items() if k != 'geomean'])},

        'total_energy': np.average([v['total_energy'] for k, v in design.items() if k != 'geomean']),
        'relative_total_energy': np.average([v['relative_total_energy'] for k, v in design.items() if k != 'geomean']),
    }

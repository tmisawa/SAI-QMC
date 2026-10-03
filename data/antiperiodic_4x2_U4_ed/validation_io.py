"""Strict readers shared by the pre-registered analysis and restart driver.

Structural/condition errors are INVALID. Non-finite numerical observations or
references are HOLD; they must never be silently replaced or discarded.
"""
import json
import math
from pathlib import Path

QS = [(mx, my) for my in range(2) for mx in range(4)]
Q = (2, 1)
BIN_COLUMNS = ('beta_index beta_requested beta_effective Ltr replica_id seed bin_id '
               'sweep_begin sweep_end count sum_sign sum_sign_Ehub sum_sign_D '
               'local_accepted local_attempts global_accepted global_attempts '
               'sum_sign_Szz_Q sum_sign_Sperp_Q sum_sign_Szz_0').split()


class DataError(ValueError):
    pass


class NumericHold(Exception):
    pass


def require(ok, message):
    if not ok:
        raise DataError(message)


def number(value, label):
    try:
        require(not isinstance(value, bool), label + ': boolean is not numeric')
        x = float(value)
    except (ValueError, TypeError, OverflowError) as exc:
        raise DataError(label + ': invalid number') from exc
    if not math.isfinite(x):
        raise NumericHold(label + ': non-finite number')
    return x


def integer(value, label):
    x = number(value, label)
    require(x.is_integer(), label + ': expected integer')
    # Preserve all 64 seed bits; converting the original decimal via float loses them.
    try:
        return int(value)
    except (ValueError, TypeError, OverflowError) as exc:
        raise DataError(label + ': expected integer') from exc


def close(a, b, rel=1e-12, absolute=0.0):
    return (math.isfinite(a) and math.isfinite(b) and
            abs(a-b) <= max(absolute, rel*max(abs(a), abs(b))))


def header(lines, prefix, label):
    found = [line[2:] for line in lines if line.startswith(prefix)]
    require(len(found) == 1, label + ': missing or duplicate ' + prefix.strip())
    tokens = found[0].split()
    require(all(t.count('=') == 1 for t in tokens), label + ': malformed header')
    pairs = [t.split('=', 1) for t in tokens]
    require(len({k for k, _ in pairs}) == len(pairs), label + ': duplicate header key')
    return dict(pairs)


def match_header(got, expected, label):
    for key, want in expected.items():
        require(key in got, label + ': missing ' + key)
        if isinstance(want, int):
            require(integer(got[key], label + ':' + key) == want, label + ': wrong ' + key)
        elif isinstance(want, float):
            require(close(number(got[key], label + ':' + key), want, absolute=1e-12),
                    label + ': wrong ' + key)
        else:
            require(got[key] == want, label + ': wrong ' + key)


def metadata(lines, dtau, label, spin=False):
    got = header(lines, '# lattice=', label)
    require('pbc' not in got, label + ': legacy pbc is not AP/P')
    want = dict(lattice='square', Lx=4, Ly=2, n=8, bc_x='antiperiodic',
                bc_y='periodic', U=4.0, dtau=dtau)
    if spin:
        want.update(t=-1.0, mu=2.0)
    else:
        want.update(nwarm=2000, nmeas=40000, nbin=20,
                    global_update='none', global_interval=100)
    match_header(got, want, label)


def read_q_file(path, dtau, beta, seed, channel):
    label = f'{path.parent.name}/{path.name}'
    lines = path.read_text().splitlines()
    metadata(lines, dtau, label, spin=True)
    got = header(lines, '# ' + channel + '_q=', label)
    match_header(got, {channel+'_q': 'all', 'seed': seed, 'parallel': 'serial',
                       'nrep': 1, 'bins': 20, 'nbeta': 1}, label)
    columns = ('beta_requested beta T q_index mx my qx_over_pi qy_over_pi '
               'qx_folded_over_pi qy_folded_over_pi ' +
               ('Szz dSzz' if channel == 'szz' else 'Sperp dSperp'))
    require(lines.count('# '+columns) == 1, label + ': wrong spin columns')
    rows = [l.split() for l in lines if l.strip() and not l.startswith('#')]
    require(len(rows) == 8 and all(len(r) == 12 for r in rows), label + ': expected 8 rows of 12 columns')
    values = {}
    for f in rows:
        qid, mx, my = [integer(f[k], label) for k in (3, 4, 5)]
        require(0 <= qid < 8 and (mx, my) == QS[qid] and (mx, my) not in values,
                label + ': wrong or duplicate q index/coordinates')
        for k, want in [(0, beta), (1, beta), (2, 1/beta), (6, mx/2), (7, float(my)),
                        (8, (mx if mx <= 2 else mx-4)/2), (9, float(my))]:
            require(close(number(f[k], label), want, absolute=1e-12), label + ': wrong beta or momentum')
        values[(mx, my)] = number(f[10], label + ': observable')
        require(number(f[11], label + ': error') >= 0, label + ': negative error')
    return values


def series_values(directory, dtau, beta, seed):
    """Validate all bin/spin metadata and return values plus channel mismatches."""
    d = Path(directory)
    label = f'runs/dt{dtau:g}/b{beta:g}/{d.name}'
    require((d/'exit_code.txt').read_text().strip() == '0', label + ': nonzero exit')
    lines = (d/'bins.tsv').read_text().splitlines()
    metadata(lines, dtau, label)
    qline = '# szz_Q_index=6 szz_0_index=0 sperp_Q_index=6'
    require([l for l in lines if l.startswith('# szz_Q_index=')] == [qline], label + ': wrong q indices')
    require([l for l in lines if l.startswith('# columns:')] ==
            ['# columns: '+'\t'.join(BIN_COLUMNS)], label + ': wrong bin columns')
    rows = [line.split('\t') for line in lines if line.strip() and not line.startswith('#')]
    require(len(rows) == 20 and all(len(r) == 20 for r in rows), label + ': expected 20 bins of 20 columns')
    ids = set()
    for row in rows:
        bid = integer(row[6], label)
        require(0 <= bid < 20 and bid not in ids, label + ': wrong or duplicate bin ID')
        ids.add(bid)
        for col, expected in [(0, 0), (3, round(beta/dtau)), (4, 0), (5, seed),
                              (7, bid*2000+1), (8, (bid+1)*2000), (9, 2000)]:
            require(integer(row[col], label) == expected, label + ': wrong bin metadata')
        for col in (1, 2):
            require(close(number(row[col], label), beta, absolute=1e-12), label + ': wrong beta')
        for col in (13, 14, 15, 16):
            require(integer(row[col], label) >= 0, label + ': negative counter')
        require(integer(row[13], label) <= integer(row[14], label), label + ': invalid acceptance counts')
        require(integer(row[15], label) == integer(row[16], label) == 0, label + ': unexpected global updates')
        require(close(number(row[10], label + ': sign'), 2000.0), label + ': non-positive or non-unit sign count')
    sums = {k: sum(number(row[k], label + ': '+BIN_COLUMNS[k]) for row in rows)
            for k in (10, 11, 12, 17, 18, 19)}
    for x in sums.values():
        number(x, label + ': accumulated sum')
    require(sums[10] > 0, label + ': non-positive sign denominator')
    require(close(sums[10], 40000.0), label + ': expected sign-free measurement count')
    ws = sums[10]
    values = {'E/N': sums[11]/ws/8, 'D': sums[12]/ws,
              'Szz(Q)': sums[17]/ws, 'Sperp(Q)/2': sums[18]/ws/2}
    zz = read_q_file(d/'szz.dat', dtau, beta, seed, 'szz')
    pp = read_q_file(d/'sperp.dat', dtau, beta, seed, 'sperp')
    mismatches = []
    for channel, a, b in [('Szz', zz[Q], values['Szz(Q)']),
                          ('Sperp', pp[Q]/2, values['Sperp(Q)/2'])]:
        if not close(a, b):
            mismatches.append(label + ': ' + channel + '(Q) file/bin mismatch')
    for mx, my in QS:
        values[f'Szz({mx},{my})'] = zz[(mx, my)]
        values[f'Sperp({mx},{my})/2'] = pp[(mx, my)]/2
    return values, mismatches


def validate_ed(data, interaction, expected_hopping, label):
    """Validate all reference rows, including finite values and unique beta/q grids."""
    require(isinstance(data, dict), label + ': expected ED object')
    match_header(data, dict(sites=8, Lx=4, Ly=2, U=float(interaction), mu=interaction/2), label)
    require(data.get('hopping') == expected_hopping, label + ': hopping identity mismatch')
    require(isinstance(data.get('results'), list) and len(data['results']) == 2,
            label + ': expected beta=2,4')
    result = {}
    for row in data['results']:
        require(isinstance(row, dict), label + ': expected ED row')
        try:
            beta = number(row['beta'], label + ': beta')
            require(beta in (2.0, 4.0) and beta not in result, label + ': wrong or duplicate beta')
            for key in ('E_hub_per_site', 'doublon_per_site', 'ntot'):
                number(row[key], label + ':' + key)
            require(abs(row['ntot']-8) <= 1e-8, label + ': not half filled')
            require(isinstance(row['Szz'], list) and len(row['Szz']) == 8, label + ': expected 8 ED q entries')
            qs = set()
            for q in row['Szz']:
                require(isinstance(q, dict), label + ': invalid ED q entry')
                key = (integer(q['mx'], label), integer(q['my'], label))
                require(key in QS and key not in qs, label + ': wrong or duplicate ED q')
                qs.add(key)
                number(q['value'], label + ': Szz')
            result[beta] = row
        except (KeyError, TypeError) as exc:
            raise DataError(label + ': missing or malformed ED fields') from exc
    return result


def read_ed(path, expected_hopping, interaction=4.0):
    path = Path(path)
    return validate_ed(json.loads(path.read_text()), interaction, expected_hopping, path.name)

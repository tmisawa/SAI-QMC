#!/usr/bin/env python3
"""Regression tests using synthetic data, never physical validation results.

Run before pre-registration: python3 -B test_validation.py
Uses only the standard library; does not execute the solver or ED.
"""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import analyze_antiperiodic_ed as analysis
import check_ed_reference as checker
import run_validation as driver
import run_completion as completion
import validation_io as vio

MATRIX = ('8\n0 -1 0 1 -2 0 0 0\n-1 0 -1 0 0 -2 0 0\n0 -1 0 -1 0 0 -2 0\n'
          '1 0 -1 0 0 0 0 -2\n-2 0 0 0 0 -1 0 1\n0 -2 0 0 -1 0 -1 0\n'
          '0 0 -2 0 0 -1 0 -1\n0 0 0 -2 1 0 -1 0\n')
BINARY = hashlib.sha256(b'synthetic binary identity').hexdigest()


def reference(periodic=False):
    return dict(sites=8, Lx=4, Ly=2, U=4.0, mu=2.0,
                hopping='hopping_pp.txt' if periodic else 'hopping_app.txt',
                results=[dict(beta=beta, E_hub_per_site=-0.9 if periodic else -1.0,
                              doublon_per_site=0.18 if periodic else 0.15, ntot=8.0,
                              Szz=[dict(mx=mx, my=my, value=(0.125 if periodic else 0.14)+0.01*k)
                                   for k, (mx, my) in enumerate(vio.QS)]) for beta in (2.0, 4.0)])


def fixture(root):
    (root/'hopping_app.txt').write_text(MATRIX)
    hop = completion.sha256(root/'hopping_app.txt')
    for name, periodic in [('app', False), ('pp', True)]:
        (root/f'ed_{name}.json').write_text(json.dumps(reference(periodic)))
    for dt, beta, _b, r, _base, seed in driver.seed_table():
        d = root/'runs'/f'dt{dt:g}'/f'b{beta:g}'/f'r{r:02d}'
        d.mkdir(parents=True)
        (d/'input.in').write_text(driver.expected_input(dt, beta, seed))
        (d/'hopping_used.txt').write_text(MATRIX)
        (d/'hopping_used.sha256').write_text(hop+'\n')
        (d/'exit_code.txt').write_text('0\n')
        for name in ('observables.dat', 'stdout.txt'):
            (d/name).write_text('# synthetic test artifact\n1 2 3\n')
        (d/'stderr.txt').write_text('')
        shift = 0.02*dt*dt + (r-7.5)*0.0001
        energy, doublon = -1.0+shift, 0.15+shift
        zz = [0.14+0.01*k+shift for k in range(8)]
        common = (f'# lattice=square Lx=4 Ly=2 n=8 bc_x=antiperiodic bc_y=periodic '
                  f'U=4 dtau={dt:.17g}')
        lines = [common+' nwarm=2000 nmeas=40000 nbin=20 global_update=none global_interval=100',
                 '# szz_Q_index=6 szz_0_index=0 sperp_Q_index=6',
                 '# columns: '+'\t'.join(vio.BIN_COLUMNS)]
        for b in range(20):
            row = [0,beta,beta,round(beta/dt),0,seed,b,b*2000+1,(b+1)*2000,2000,2000,
                   16000*energy,2000*doublon,1000,2000,0,0,2000*zz[6],4000*zz[6],2000*zz[0]]
            lines.append('\t'.join(map(str,row)))
        (d/'bins.tsv').write_text('\n'.join(lines)+'\n')
        for channel, factor, column in [('szz',1,'Szz'),('sperp',2,'Sperp')]:
            lines = [common+' t=-1 mu=2',
                     f'# {channel}_q=all seed={seed} parallel=serial nrep=1 bins=20 nbeta=1',
                     '# beta_requested beta T q_index mx my qx_over_pi qy_over_pi '
                     f'qx_folded_over_pi qy_folded_over_pi {column} d{column}']
            for k,(mx,my) in enumerate(vio.QS):
                lines.append(' '.join(map(str,[beta,beta,1/beta,k,mx,my,mx/2,my,
                                              (mx if mx<=2 else mx-4)/2,my,factor*zz[k],0.001])))
            (d/f'{channel}.dat').write_text('\n'.join(lines)+'\n')
        expected = completion.context(d,BINARY)
        completion.write_record(d/'started.json',{'context':expected})
        completion.finish(d,expected,dt,beta,seed,hop)


class ValidationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        fixture(cls.root)
        cls.d = cls.root/'runs/dt0.1/b2/r00'
        cls.seed = driver.seed_table()[0][-1]
        cls.hop = completion.sha256(cls.root/'hopping_app.txt')

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def invoke(self):
        with patch.object(analysis,'HERE',self.root), patch.object(driver,'HERE',self.root), \
             patch.object(sys,'argv',['analyze_antiperiodic_ed.py']), contextlib.redirect_stdout(io.StringIO()):
            rc = analysis.main()
        result = json.loads((self.root/'summary.json').read_text())
        self.assertIn('verdict: '+result['verdict'], (self.root/'analysis_output.md').read_text())
        self.assertEqual(rc, {'PASS':0,'FAIL':1,'HOLD':2,'INVALID':3}[result['verdict']])
        return rc

    @contextlib.contextmanager
    def change(self, path, transform, reseal=False):
        old = path.read_bytes()
        manifest = self.d/'complete.json'
        old_marker = manifest.read_bytes()
        try:
            path.write_text(transform(old.decode()))
            if reseal:
                obj=json.loads(old_marker)
                obj['outputs'][path.name]=completion.sha256(path)
                completion.write_record(manifest,obj)
            yield
        finally:
            path.write_bytes(old)
            manifest.write_bytes(old_marker)

    def test_normal_and_wrong_reference(self):
        self.assertEqual(self.invoke(),0)
        def wrong_numeric_reference(text):
            obj = json.loads(text)
            obj['results'] = reference(periodic=True)['results']
            return json.dumps(obj)
        with self.change(self.root/'ed_app.json', wrong_numeric_reference):
            self.assertEqual(self.invoke(),1)

    def test_ed_hopping_provenance(self):
        for mode in ('missing', 'wrong'):
            def mutate(text):
                obj = json.loads(text)
                if mode == 'missing':
                    del obj['hopping']
                else:
                    obj['hopping'] = 'hopping_pp.txt'
                return json.dumps(obj)
            with self.subTest(mode=mode), self.change(self.root/'ed_app.json', mutate):
                self.assertEqual(self.invoke(),3)

    def test_nonfinite_references(self):
        for filename in ('ed_app.json','ed_pp.json'):
            for field in ('E_hub_per_site','doublon_per_site','ntot','Szz'):
                for value in (float('nan'),float('inf'),-float('inf')):
                    def mutate(text):
                        obj=json.loads(text)
                        if field=='Szz': obj['results'][1]['Szz'][0]['value']=value
                        else: obj['results'][1][field]=value
                        return json.dumps(obj)
                    with self.subTest(file=filename,field=field,value=value), self.change(self.root/filename,mutate):
                        self.assertEqual(self.invoke(),2)
                        hopping = 'hopping_pp.txt' if filename == 'ed_pp.json' else 'hopping_app.txt'
                        with self.assertRaises(vio.NumericHold):
                            vio.read_ed(self.root/filename, hopping)

    def test_ed_schema(self):
        for field in ('mu','U','sites'):
            with self.subTest(field=field):
                with self.change(self.root/'ed_app.json',lambda t: json.dumps({k:v for k,v in json.loads(t).items() if k!=field})):
                    self.assertEqual(self.invoke(),3)
        with self.change(self.root/'ed_app.json',lambda t:t.replace('"mx": 1','"mx": 0',1)):
            self.assertEqual(self.invoke(),3)

    def test_ed_checker_rejects_nonfinite(self):
        free = reference()
        free.update(U=0.0, mu=0.0)
        free['hopping'] = str(self.root/'hopping_app.txt')
        for row in free['results']:
            row['E_hub_per_site'], values = checker.free_values(row['beta'])
            row['doublon_per_site'] = 0.25
            for q in row['Szz']:
                q['value'] = values[q['mx'], q['my']]
        with patch.object(checker, 'HERE', self.root), contextlib.redirect_stdout(io.StringIO()):
            for value in (None, float('nan'), float('inf'), -float('inf')):
                for filename in ('ed_app.json', 'ed_pp.json', 'U0'):
                    if value is None:
                        with patch.object(checker.subprocess, 'run', return_value=SimpleNamespace(stdout=json.dumps(free))):
                            self.assertEqual(checker.main(), 0)
                        continue
                    if filename == 'U0':
                        obj = json.loads(json.dumps(free))
                        obj['results'][0]['Szz'][0]['value'] = value
                        with patch.object(checker.subprocess, 'run', return_value=SimpleNamespace(stdout=json.dumps(obj))):
                            with self.assertRaises(vio.NumericHold): checker.main()
                    else:
                        def mutate(text):
                            obj = json.loads(text)
                            obj['results'][0]['ntot'] = value
                            return json.dumps(obj)
                        with self.change(self.root/filename, mutate), \
                             patch.object(checker.subprocess, 'run', return_value=SimpleNamespace(stdout=json.dumps(free))):
                            with self.assertRaises(vio.NumericHold): checker.main()

    def test_ed_checker_rejects_wrong_hopping(self):
        free = reference()
        free.update(U=0.0, mu=0.0, hopping=str(self.root/'hopping_app.txt'))
        for row in free['results']:
            row['E_hub_per_site'], values = checker.free_values(row['beta'])
            row['doublon_per_site'] = 0.25
            for q in row['Szz']:
                q['value'] = values[q['mx'], q['my']]
        with patch.object(checker, 'HERE', self.root), contextlib.redirect_stdout(io.StringIO()):
            for mode in ('missing', 'wrong'):
                obj = json.loads(json.dumps(free))
                if mode == 'missing':
                    del obj['hopping']
                else:
                    obj['hopping'] = 'hopping_app.txt'
                with self.subTest(source='U0', mode=mode), \
                     patch.object(checker.subprocess, 'run', return_value=SimpleNamespace(stdout=json.dumps(obj))):
                    with self.assertRaises(vio.DataError):
                        checker.main()
            for filename, wrong in (('ed_app.json', 'hopping_pp.txt'),
                                    ('ed_pp.json', 'hopping_app.txt')):
                with self.subTest(source=filename), self.change(
                        self.root/filename,
                        lambda text, wrong=wrong: json.dumps(dict(json.loads(text), hopping=wrong))), \
                     patch.object(checker.subprocess, 'run', return_value=SimpleNamespace(stdout=json.dumps(free))):
                    with self.assertRaises(vio.DataError):
                        checker.main()

    def test_driver_run_resume_and_bound_context(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root/'hopping_app.txt').write_text(MATRIX)
            binary = root/'dqmc'
            binary.write_bytes(b'synthetic binary identity')
            d = root/'runs/dt0.1/b2/r00'
            d.mkdir(parents=True)
            (d/'input.in').write_bytes((self.d/'input.in').read_bytes())
            def fake_solver(directory, _binary):
                for name in completion.OUTPUTS + ('hopping_used.txt',):
                    if name != 'hopping_used.sha256':
                        shutil.copyfile(self.d/name, directory/name)
                return 0
            with patch.object(driver, 'HERE', root), \
                 patch.object(driver, 'seed_table', return_value=[driver.seed_table()[0]]), \
                 patch.object(driver, 'run_dqmc', side_effect=fake_solver) as solver, \
                 contextlib.redirect_stdout(io.StringIO()):
                args = SimpleNamespace(dqmc=binary)
                driver.cmd_run(args)
                self.assertEqual(solver.call_count, 1)
                self.assertTrue((d/'complete.json').is_file())
                driver.cmd_run(args)
                self.assertEqual(solver.call_count, 1)
                (d/'hopping_used.sha256').unlink()
                driver.cmd_run(args)
                self.assertEqual(solver.call_count, 1)
                (d/'sperp.dat').unlink()
                driver.cmd_run(args)
                self.assertEqual(solver.call_count, 2)
                self.assertTrue((d/'attempts/0001/bins.tsv').is_file())
                logs = [json.loads(line) for line in (root/'run_log.jsonl').read_text().splitlines()]
                self.assertEqual([len(x['recovered']) for x in logs], [0, 0, 1, 0])
                self.assertEqual([len(x['skipped']) for x in logs], [0, 1, 0, 0])
                binary.write_bytes(b'changed binary')
                with self.assertRaises(vio.DataError): driver.cmd_run(args)
                self.assertEqual(solver.call_count, 2)

    def test_both_spin_consistency_checks(self):
        for channel in ('szz','sperp'):
            def mutate(text):
                lines=[]
                for line in text.splitlines():
                    if not line.startswith('#'):
                        f=line.split()
                        if f[3]=='6': f[10]=str(float(f[10])*1.00001)
                        line=' '.join(f)
                    lines.append(line)
                return '\n'.join(lines)+'\n'
            with self.subTest(channel=channel), self.change(self.d/f'{channel}.dat',mutate,True):
                self.assertEqual(self.invoke(),2)
        with self.change(self.d/'szz.dat', mutate, True), self.change(self.d/'sperp.dat', mutate, True):
            self.assertEqual(self.invoke(), 2)

    @staticmethod
    def bin_column(text,col,value):
        lines=[]
        for line in text.splitlines():
            if not line.startswith('#'):
                f=line.split('\t'); f[col]=str(value); line='\t'.join(f)
            lines.append(line)
        return '\n'.join(lines)+'\n'

    def test_bin_metadata_and_sign(self):
        for col,value in [(0,1),(1,999),(2,999),(3,999),(4,1),(5,42),(6,0),
                          (7,0),(8,0),(9,1),(10,0),(13,9999),(15,1),(16,1)]:
            with self.subTest(col=col), self.change(self.d/'bins.tsv',lambda t:self.bin_column(t,col,value),True):
                self.assertEqual(self.invoke(),3)

    def test_bin_nonfinite_hold(self):
        with self.change(self.d/'bins.tsv',lambda t:self.bin_column(t,11,'nan'),True):
            self.assertEqual(self.invoke(),2)

    def test_headers_rows_columns(self):
        def duplicate_q(text):
            lines = text.splitlines()
            first = next(line for line in lines if not line.startswith('#'))
            lines[-1] = first
            return '\n'.join(lines)+'\n'
        mutations=[('bins.tsv',lambda t:t.replace('bc_x=antiperiodic bc_y=periodic','pbc=1')),
                   ('bins.tsv',lambda t:t.replace('nmeas=40000','nmeas=20')),
                   ('bins.tsv',lambda t:'\n'.join(l if l.startswith('#') else '\t'.join(l.split('\t')[:6]) for l in t.splitlines())+'\n'),
                   ('bins.tsv',lambda t:'\n'.join(t.splitlines()[:-1])+'\n'),
                   ('szz.dat',lambda t:t+t.splitlines()[-1]+'\n'),
                   ('szz.dat',duplicate_q),
                   ('sperp.dat',duplicate_q),
                   ('sperp.dat',lambda t:t.replace('parallel=serial','parallel=mpi')),
                   ('szz.dat',lambda t:t.replace('seed='+str(self.seed),'seed=123')),
                   ('szz.dat',lambda t:t.replace('2.0 2.0 0.5','999 2.0 0.5'))]
        for name,mutation in mutations:
            with self.subTest(name=name,mutation=mutation), self.change(self.d/name,mutation,True):
                self.assertEqual(self.invoke(),3)

    def test_artifact_checksum_and_stale_pass(self):
        self.assertEqual(self.invoke(),0)
        with self.change(self.d/'szz.dat',lambda t:t+'\n'):
            self.assertEqual(self.invoke(),3)
            self.assertNotIn('verdict: PASS',(self.root/'analysis_output.md').read_text())

    def isolated_run(self,root):
        d=Path(root)/'r00';shutil.copytree(self.d,d)
        return d,completion.context(d,BINARY)

    def test_completed_skip_and_digest_recovery(self):
        for marker_present in (True,False):
            with self.subTest(marker_present=marker_present), tempfile.TemporaryDirectory() as root:
                d,c=self.isolated_run(root)
                self.assertEqual(completion.prepare(d,c,0.1,2.0,self.seed,self.hop),'skip')
                if not marker_present: (d/'complete.json').unlink()
                (d/'hopping_used.sha256').unlink()
                self.assertEqual(completion.prepare(d,c,0.1,2.0,self.seed,self.hop),'recovered')
                completion.verify(d,c,0.1,2.0,self.seed,self.hop)

    def test_archive_incomplete_and_corrupt_outputs(self):
        for mode in ('missing_matrix','missing_spin','bad_digest','bad_columns'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as root:
                d,c=self.isolated_run(root)
                if mode=='missing_matrix':
                    for name in ('complete.json','hopping_used.txt','hopping_used.sha256'): (d/name).unlink()
                elif mode=='missing_spin': (d/'szz.dat').unlink()
                elif mode=='bad_digest': (d/'hopping_used.sha256').write_text('bad\n')
                else: (d/'bins.tsv').write_text('broken\n')
                old={p.name:p.read_bytes() for p in d.iterdir() if p.is_file() and p.name!='input.in'}
                self.assertEqual(completion.prepare(d,c,0.1,2.0,self.seed,self.hop),'run')
                self.assertTrue((d/'input.in').is_file())
                self.assertEqual(old,{p.name:p.read_bytes() for p in (d/'attempts/0001').iterdir()})

    def test_changed_binary_or_input_rejected(self):
        for key in ('dqmc_sha256','input_sha256'):
            with self.subTest(key=key), tempfile.TemporaryDirectory() as root:
                d,c=self.isolated_run(root);c[key]='f'*64
                with self.assertRaises(vio.DataError): completion.prepare(d,c,0.1,2.0,self.seed,self.hop)
                self.assertFalse((d/'attempts').exists())

    def test_interruption_before_marker(self):
        with tempfile.TemporaryDirectory() as root:
            d,c=self.isolated_run(root);(d/'complete.json').unlink()
            with patch.object(completion,'write_record',side_effect=RuntimeError('simulated interruption')):
                with self.assertRaises(RuntimeError): completion.finish(d,c,0.1,2.0,self.seed,self.hop)
            self.assertEqual(completion.prepare(d,c,0.1,2.0,self.seed,self.hop),'recovered')

    def test_numeric_hold_is_never_retried(self):
        for mode in ('nonfinite', 'spin_mismatch'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as root:
                d,c=self.isolated_run(root);(d/'complete.json').unlink()
                col, value = (11, 'nan') if mode == 'nonfinite' else (18, '999')
                (d/'bins.tsv').write_text(self.bin_column((d/'bins.tsv').read_text(),col,value))
                completion.finish(d,c,0.1,2.0,self.seed,self.hop)
                self.assertEqual(completion.prepare(d,c,0.1,2.0,self.seed,self.hop),'skip')
                self.assertTrue(json.loads((d/'complete.json').read_text())['numerical_holds'])


if __name__=='__main__':
    unittest.main(verbosity=2)

"""Test C7 native-loop control flow with an explicit non-game evaluator fixture.

Usage: python c7_resume.py --executable PATH --out NEW_SCRATCH_DIRECTORY
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys


def projection(checkpoint):
    state=checkpoint['state']
    def cell(value):
        return {k:v for k,v in value.items() if k not in {'response_path','response_sha256'}}
    def row(value):
        result={k:v for k,v in value.items() if k not in {'cells','validation'}}
        result['cells']={k:cell(v) for k,v in value['cells'].items()}
        result['validation']={k:cell(v) for k,v in value['validation'].items()}
        return result
    return {'iteration':checkpoint['iteration'],'rng':checkpoint['rng'],
            'rows':[row(v) for v in state['rows']],
            'columns':state['columns'],'seen':state['seen'],
            'pending':None if state['pending'] is None else row(state['pending']),
            'candidate_count':state['candidate_count'],'last_solution':state['last_solution'],
            'validation_epoch':state['validation_epoch'],'validation_position':state['validation_position'],
            'validation_queue':state['validation_queue'],'proposal_seed_position':state['proposal_seed_position']}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    root=args.out.resolve()
    root.mkdir(parents=True,exist_ok=False)
    fixture_repo=root/'repo'
    fixture_repo.mkdir()
    evaluator=Path(__file__).with_name('c7_fixture_evaluator.py').resolve()
    config={
        'controller':'fixture/controller','baseline':'fixture/base','opponents':['fixture/rock'],
        'families':[{'id':'train','maps':['train-a']}],
        'validation_opponents':['fixture/scissors'],
        'validation_families':[{'id':'held-out','maps':['validation-a']}],
        'validation_every':1,'sides':['A','B'],'matrix_steps':2000,
        'admit_generated_opponents':True,'admission_margin':0.0,'max_policies':16,'max_opponents':16,
        'proposal_seeds':[{'draft_mode':1},{'draft_mode':2},{'draft_mode':3},{'draft_mode':5}],
        'search':{'space':[{'name':'draft_mode','values':[0,1,2,3,5],'default':0}]}}
    input_file=root/'config.json'
    input_file.write_text(json.dumps(config),encoding='utf-8')
    command=[str(args.executable.resolve()),'c7','--input',str(input_file),'--repo',str(fixture_repo),
             '--seed','17','--evaluator',sys.executable,'--evaluator-arg',str(evaluator)]
    def launch(name,cap,resume=False,expected=0):
        result=subprocess.run(command+['--out',str(root/name),'--iterations',str(cap)]+(['--resume'] if resume else []),capture_output=True,text=True)
        (root/(name+f'-{cap}-'+('resume' if resume else 'start')+'.stdout')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==expected,(result.returncode,result.stdout,result.stderr)
        return json.loads((root/name/'checkpoint.json').read_text())
    uninterrupted=launch('uninterrupted',50)
    launch('split',5)
    split=launch('split',50,True)
    assert projection(uninterrupted)==projection(split),'C7 restart changed search, admission, mixture or RNG trajectory'
    assert len(split['state']['rows'])>=3 and len(split['state']['columns'])>=3,'fixture never exercised league expansion'
    assert split['state']['candidate_count']==4
    assert all(row['knobs']['draft_mode']!=5 for row in split['state']['rows']),'unhealthy exploiter entered mixture'
    report=json.loads((root/'split/report.json').read_text())
    mixture=json.loads((root/'split/mixture.json').read_text())
    assert mixture['policy_artifacts']==report['policy_artifacts'] and mixture['validation']==report['validation'],'mixture export lost completed fields'
    assert (root/'split/deployment.json').is_file() and (root/'split/best-config.hpp').is_file()
    assert all((root/'split'/value['response_path']).is_file() for row in split['state']['rows'] for value in row['cells'].values()),'raw native-loop responses are not preserved'
    before=projection(split)
    assert before==projection(launch('split',50,True)),'completed replay changed league fitness'
    # Validation infrastructure failure is an explicit stop, but cannot remove a
    # healthy training strategy or rewrite its training cells.
    faulty=dict(config)
    faulty['evaluation']={'fixture_validation_infrastructure_failure':True}
    input_file.write_text(json.dumps(faulty),encoding='utf-8')
    fault=launch('validation-fault',50,expected=2)
    assert fault['state']['stop_reason']=='blocked_opponent_or_baseline_health'
    assert all(row['eligible'] for row in fault['state']['rows']),'validation health contaminated training eligibility'
    assert all(cell['subject_healthy'] and cell['infrastructure_healthy'] for row in fault['state']['rows'] for cell in row['cells'].values())
    fault_report=json.loads((root/'validation-fault/report.json').read_text())
    assert any(not row['complete'] and row['payoff'] is None for row in fault_report['validation_mixture']),'unhealthy holdout became zero-valued evidence'
    assert projection(fault)==projection(launch('validation-fault',50,True,expected=2)),'health block continued after reopening'
    input_file.write_text(json.dumps(config),encoding='utf-8')
    victim=next(value for row in split['state']['rows'] for value in row['cells'].values())
    with (root/'split'/victim['response_path']).open('ab') as stream:
        stream.write(b'\n')
    launch('split',50,True,expected=1)
    print('C7 strict admission, generated-opponent restore, exact split/resume, warm starts, health isolation, full exports, terminal health blocks and corrupt-response rejection passed')


if __name__=='__main__':
    main()

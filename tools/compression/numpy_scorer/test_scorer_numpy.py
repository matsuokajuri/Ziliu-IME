"""Opt-in synthetic arrays only; no weights, loader, data files or forwards.
Run explicitly after your environment/resource checks: python test_scorer_numpy.py
Discovery/import does not import NumPy or execute array tests.
"""
from synthetic_qualification import run

def main():
    import numpy as np
    seen=[]
    result=run(np,seen.append)
    if result['fixture_count']!=8 or not all(x['passed'] for x in result['checks']):
        raise AssertionError('fixture completeness')
    if seen[0]!='before_synthetic' or len(seen)!=9:
        raise AssertionError('observation hooks')
    print('PASS: 8 synthetic primitive fixtures; zero model forwards')

if __name__=='__main__':main()

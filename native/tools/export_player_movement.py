#!/usr/bin/env python3.13
"""Fresh original hero bank adding actual native movement start/stop/walk clips."""
import argparse,json
from pathlib import Path
from export_actor_animations import export
p=argparse.ArgumentParser(description=__doc__)
for k in ['content','decoder','previous','output']:p.add_argument('--'+k,type=Path,required=True)
a=p.parse_args();a.directions=8;a.names=[c['name']for c in json.loads(a.previous.read_text())['clips']]+['ZagreusStart','ZagreusStop','ZagreusWalk','ZagreusWalkStop','ZagreusSwordRunStop'];a.unit_scale=None
export(a)

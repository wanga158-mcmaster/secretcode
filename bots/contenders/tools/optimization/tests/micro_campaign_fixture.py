"""Synthetic native-shaped inventory protocol for campaign orchestration tests only.

The native flag exercises the production gate; test_fixture and identity.scope
explicitly disqualify these observations as native gameplay evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path


def actor(actor_id=0, kit=2, **changes):
    value = dict(id=actor_id, team=0, type=kit, x=5, y=5, health=100,
                 maxHealth=100, alive=True, items=[], cooldowns=[0, 0],
                 availability=dict(move=True, attack=True, cast=True),
                 channel=dict(kind="none"), personalVisibleChampions=[1])
    value.update(changes)
    return value


def inventory_states():
    enemy = dict(id=1, team=1, type=3, x=7, y=5, health=100,
                 maxHealth=100, alive=True)
    states = [dict(input=i, owner=0, round=10, champions=[actor(), enemy], camps=[])
              for i in (10, 11)]
    camp = dict(id=8, group_id=7, type=2, x=6, y=7, health=55,
                maxHealth=100, visible=True, awake=True)
    farm_actor = actor(4, 10, health=20, items=[4], cooldowns=[8, 0],
                       personalVisibleChampions=[],
                       availability=dict(move=False, attack=True, cast=False),
                       channel=dict(kind="focus"))
    states.extend(dict(input=i, owner=4, round=120, champions=[farm_actor], camps=[camp])
                  for i in (30, 35))
    states.append(dict(input=50, owner=0, round=150,
                       champions=[actor(health=0, alive=False), enemy], camps=[]))
    states.append(dict(input=70, owner=0, round=200,
                       champions=[actor(personalVisibleChampions=[]), enemy], camps=[]))
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", type=Path, required=True)
    parser.add_argument("--response", type=Path, required=True)
    parser.add_argument("--repo", type=Path)
    args = parser.parse_args()
    request = json.loads(args.request.read_text(encoding="utf-8"))
    if request["kind"] != "inventory":
        raise ValueError("fixture supports only inventory requests")
    replay = Path(request["scenario"]["replay"])
    identity = dict(backend="campaign-inventory-fixture/v1",
                    replay_sha256=hashlib.sha256(replay.read_bytes()).hexdigest(),
                    scope="Synthetic protocol fixture, not a retained game or native evidence")
    response = dict(schema="contenders-micro/v1", native=True, supported=True,
                    test_fixture=True, identity=identity,
                    request_id=request.get("request_id"),
                    states=[state for state in inventory_states()
                            if request["first_input"] <= state["input"] <= request["last_input"]])
    if log := os.environ.get("MICRO_CAMPAIGN_FIXTURE_LOG"):
        with Path(log).open("a", encoding="utf-8", newline="\n") as stream:
            stream.write(json.dumps(request, sort_keys=True) + "\n")
    args.response.parent.mkdir(parents=True, exist_ok=True)
    args.response.write_text(json.dumps(response, sort_keys=True), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

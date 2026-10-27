import pytest

from psv_remote.codex_state import Replica, StateError


def snapshot(replica, revision=1):
    replica.apply({"type": "snapshot", "revision": revision,
                   "conversationState": {"id": "thread", "items": ["first"]}})


def patch(replica, revision, base, patches):
    replica.apply({"type": "patches", "revision": revision,
                   "baseRevision": base, "patches": patches})


def test_revision_gap_disables_controls_until_fresh_snapshot():
    replica = Replica("thread")
    snapshot(replica)
    with pytest.raises(StateError):
        patch(replica, 4, 3, [])
    assert replica.state is None
    snapshot(replica, 2)
    assert replica.state is None
    snapshot(replica, 4)
    assert replica.state["id"] == "thread"


def test_patch_batch_is_atomic_and_cannot_change_identity():
    replica = Replica("thread")
    snapshot(replica)
    with pytest.raises(StateError):
        patch(replica, 2, 1, [{"op": "add", "path": ["items", 1], "value": "second"},
                              {"op": "replace", "path": ["id"], "value": "other"}])
    assert replica.state is None


def test_array_paths_and_duplicate_delivery():
    replica = Replica("thread")
    snapshot(replica)
    patch(replica, 2, 1, [{"op": "add", "path": ["items", 0], "value": "new"},
                          {"op": "remove", "path": ["items", 1]}])
    patch(replica, 2, 1, [{"op": "remove", "path": ["items", 0]}])
    assert replica.state["items"] == ["new"]


@pytest.mark.parametrize("path", [["items", True], ["items", -1], ["missing", "child"]])
def test_malformed_patch_invalidates_replica(path):
    replica = Replica("thread")
    snapshot(replica)
    with pytest.raises(StateError):
        patch(replica, 2, 1, [{"op": "replace", "path": path, "value": "bad"}])
    assert replica.state is None

"""Revision-checked state replication for Codex frontend coordination stream 11."""

from copy import deepcopy


class StateError(ValueError):
    pass


def _apply(document, patch):
    operation, path = patch.get("op"), patch.get("path")
    if operation not in ("add", "replace", "remove") or not isinstance(path, list):
        raise StateError("Unsupported state patch")
    if operation != "remove" and "value" not in patch:
        raise StateError("Missing patch value")
    if not path:
        if operation == "remove":
            raise StateError("Cannot remove state root")
        return deepcopy(patch["value"])
    parent = document
    for token in path[:-1]:
        if isinstance(parent, list) and type(token) is int and 0 <= token < len(parent):
            parent = parent[token]
        elif isinstance(parent, dict) and isinstance(token, str):
            parent = parent[token]
        else:
            raise StateError("Invalid patch path")
    key = path[-1]
    if isinstance(parent, list):
        if type(key) is not int or not 0 <= key <= len(parent):
            raise StateError("Invalid array index")
        if operation == "add":
            parent.insert(key, deepcopy(patch["value"]))
        elif key == len(parent):
            raise StateError("Missing array item")
        elif operation == "remove":
            parent.pop(key)
        else:
            parent[key] = deepcopy(patch["value"])
    elif isinstance(parent, dict) and isinstance(key, str):
        if operation != "add" and key not in parent:
            raise StateError("Missing object key")
        if operation == "remove":
            del parent[key]
        else:
            parent[key] = deepcopy(patch["value"])
    else:
        raise StateError("Invalid patch container")
    return document


class Replica:
    def __init__(self, thread_id: str):
        self.thread_id = thread_id
        self.state: dict | None = None
        self.revision: int | None = None
        self.minimum_revision = 0

    def apply(self, change: dict) -> None:
        try:
            revision = change.get("revision")
            if type(revision) is not int or revision < 0:
                raise StateError("Invalid revision")
            if revision < self.minimum_revision:
                return  # delayed snapshots must not revive state after a gap
            if self.state is not None and revision <= self.revision:
                return
            self.minimum_revision = max(self.minimum_revision, revision)
            if change.get("type") == "snapshot":
                candidate = deepcopy(change["conversationState"])
            elif change.get("type") == "patches":
                base, patches = change.get("baseRevision"), change.get("patches")
                if self.state is None or type(base) is not int or base != self.revision:
                    raise StateError("State revision gap; a snapshot is required")
                if not isinstance(patches, list):
                    raise StateError("Invalid patch batch")
                candidate = deepcopy(self.state)
                for patch in patches:
                    if not isinstance(patch, dict):
                        raise StateError("Invalid patch")
                    candidate = _apply(candidate, patch)
            else:
                raise StateError("Unknown state change")
            if not isinstance(candidate, dict) or candidate.get("id") != self.thread_id:
                raise StateError("State belongs to another thread")
        except (KeyError, TypeError, ValueError, IndexError) as error:
            self.state, self.revision = None, None
            raise StateError(str(error)) from error
        self.state, self.revision = candidate, revision

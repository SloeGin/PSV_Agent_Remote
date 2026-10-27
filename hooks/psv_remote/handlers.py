"""Compatibility entry point for installed Claude Code hooks."""

from .adapters.claude import handle_event

__all__ = ["handle_event"]

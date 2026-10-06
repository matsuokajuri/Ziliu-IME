"""Source-contract guard for host selection preservation during TSF preedit.

This runs without a Windows SDK. It checks the production edit method's control
flow contract, NOT real TSF behavior or Windows compilation. The release gate
still requires selected-text replacement and cancellation in native apps.
"""
from pathlib import Path
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "src/tsf/src/text_service.cpp"


def composition_edit():
    source = SOURCE.read_text(encoding="utf-8")
    start = source.index("HRESULT TextService::ApplyCompositionEdit(")
    end = source.index("void TextService::ClearCommittedPairCaret()", start)
    return source[start:end]


class SelectionContractTests(unittest.TestCase):
    def test_noncommit_exits_before_selection_mutation(self):
        body = composition_edit()
        # Anchor calculation is still needed while showing a preedit candidate.
        anchor_end = body.index("static_cast<void>(view->GetWnd(&state_->candidate_owner));")
        collapse = body.index("detail::CollapseInsertedRange(")
        between = body[anchor_end:collapse]
        self.assertRegex(between, r"if\s*\(commit\.empty\(\)\)\s*\{\s*return S_OK;\s*\}")
        self.assertNotIn("SetSelection(", body[:collapse])

    def test_text_insertion_remains_conditional_on_nonempty_commit(self):
        body = composition_edit()
        insertion = body.index("if (!commit.empty()) {")
        self.assertLess(insertion, body.index("range->SetText("))
        self.assertLess(insertion, body.index("insertion->InsertTextAtSelection("))
        self.assertIn("return context->SetSelection(edit_cookie, 1, &updated_selection);", body)

    def test_privacy_gate_remains_before_any_host_edit(self):
        body = composition_edit()
        denied = body.index("return E_ACCESSDENIED;")
        for operation in ("context->GetSelection(", "range->SetText(",
                          "insertion->InsertTextAtSelection(", "context->SetSelection("):
            self.assertLess(denied, body.index(operation))
        self.assertIn("state_->key_context.Get() != context", body)
        self.assertIn("privacy != state_->key_privacy", body)


if __name__ == "__main__":
    unittest.main()

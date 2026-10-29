"""One fresh, fixed four-scene ranking-fork update; no automatic retries."""
from pathlib import Path


def run(root, identity, scenes, *, model, opt, torch, np, packed, admission,
        adapter, ranking, objective, native_factory, contract, checkpoint,
        parent_validate, parent_restore, op):
    """Caller supplies reviewed exact state/materials and a scoped restore callback.

    This core does not select data or grant training permission. Every failure
    consumes the fresh root and records UNKNOWN. Full private remote backup and
    independent restore are required before any further work can be authorized.
    """
    root = Path(root); root.mkdir(mode=0o700); stage = 'PARENT_RESTORE'
    try:
        contract.validate(identity, parent_validate)
        contract.need(type(scenes) is list and [s['scene_id'] for s in scenes] == identity['schedule'][0], 'exact fixed four-scene order')
        op.put(root / 'ISSUED.json', {'schema': 'PUBLIC_RANK_FORK_ISSUED_V1',
                'identity_sha256': op.digest(identity), 'maximum_updates': 1, 'automatic_retry': False})
        parent_meta = parent_restore()
        contract.need(parent_meta['step'] == 25 and parent_meta['scene_cursor'] == 97, 'restored exact old endpoint')
        contract.need(op.digest(op.state_seal(model, opt)) == identity['parent_state_sha256'] and
                      op.digest(op.rng_snapshot(torch, np)) == identity['parent_rng_state_sha256'], 'full inherited state and RNG')
        contract.need(ranking.MARGIN == 1.0 and ranking.RANKING_WEIGHT == 1.0, 'fixed ranking specification')
        stage = 'MIGRATION_BASE'
        base_pair = checkpoint.save_pair(root, identity, 0, [], identity['parent_checkpoint_sha256'],
                        model, opt, torch, np, contract, parent_validate, op)
        checkpoint.restore_pair(base_pair, identity, model, opt, torch, np, contract, parent_validate, op, expected_previous_sha256=identity['parent_checkpoint_sha256'])
        before = op.state_seal(model, opt); rng_before = op.digest(op.rng_snapshot(torch, np))
        stage = 'ONE_UPDATE'; opt.zero_grad(set_to_none=True)
        scene_losses = []; conditions = 0; ranking_applied = 0
        for scene in scenes:
            modes = op.validate_scene(scene, {'schedule': [[scene['scene_id']]]})
            losses = []
            for mode in modes:
                for context in ([], scene['given_prefix_tokens']):
                    loss, info = admission.condition_loss(mode['graph'], scene['target'], mode['mode'], context,
                        model=model, packed=packed, native_factory=native_factory, adapter=adapter,
                        ranking=ranking, objective=objective)
                    contract.need(loss is not None and bool(torch.isfinite(loss)), 'every declared original condition remains eligible')
                    losses.append(loss); conditions += 1; ranking_applied += int(info['ranking_applied'])
            contract.need(len(losses) == 2 * len(modes) and len(losses) in (2, 4), 'complete declared scene conditions')
            scene_losses.append(losses)
        loss = admission.scene_condition_mean(scene_losses)
        loss.backward()
        contract.need(all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.parameters()), 'finite complete gradients')
        norm = torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0, error_if_nonfinite=True)
        contract.need(bool(torch.isfinite(norm)), 'finite original clip norm')
        opt.step()
        contract.need(all(torch.isfinite(p).all() for p in model.parameters()), 'finite updated parameters')
        contract.need(not model.embedding.requires_grad and model.embedding.grad is None and
                      op.state_seal(model, opt)['embedding'] == before['embedding'], 'unchanged frozen embedding')
        contract.need(op.digest(op.rng_snapshot(torch, np)) == rng_before, 'unchanged training RNG')
        history = [{'logical_update': 1, 'adam_step': 26, 'scenes': 4, 'conditions': conditions,
                    'loss': float(loss.detach()), 'gradient_norm': float(norm),
                    'ranking_applied_conditions': ranking_applied,
                    'ranking_ineligible_conditions': conditions - ranking_applied}]
        stage = 'FORK_CHECKPOINT'
        final_pair = checkpoint.save_pair(root, identity, 1, history, base_pair['checkpoint']['sha256'],
                            model, opt, torch, np, contract, parent_validate, op)
        state = op.state_seal(model, opt); rng = op.digest(op.rng_snapshot(torch, np))
        checkpoint.restore_pair(final_pair, identity, model, opt, torch, np, contract, parent_validate, op, expected_previous_sha256=base_pair['checkpoint']['sha256'])
        contract.need(op.state_seal(model, opt) == state and op.digest(op.rng_snapshot(torch, np)) == rng, 'final exact fork restore')
        result = {'status': 'RANK_FORK_ONE_UPDATE_COMPLETE_REMOTE_BACKUP_REQUIRED',
                  'identity_sha256': op.digest(identity), 'logical_update': 1, 'adam_step': 26,
                  'scene_cursor': 4, 'parent_adam_step': 25, 'history': history,
                  'migration_pair': base_pair, 'final_pair': final_pair,
                  'state_seal': state, 'rng_state_sha256': rng,
                  'remote_backup_verified': False, 'next_update_authorized': False}
        op.put(root / 'COMPLETE.json', result)
        return result
    except BaseException as error:
        op.put(root / 'UNKNOWN.json', {'stage': stage, 'type': type(error).__name__, 'automatic_retry': False})
        raise RuntimeError('RANK_FORK_OPERATION_UNKNOWN') from None

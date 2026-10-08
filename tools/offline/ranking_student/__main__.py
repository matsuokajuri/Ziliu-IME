"""Safe entrypoint: parameter accounting only. Deliberately no train/model CLI."""
import json
from .config import Config


def main():
    config = Config()
    print(json.dumps({"parameters":config.parameter_count(), "breakdown":config.parameter_breakdown(),
                      "source_tokens":config.source_tokens,"candidate_tokens":config.candidate_tokens,
                      "runtime_loaded":False,"trained":False,"production_enabled":False},indent=2))


if __name__ == "__main__":
    main()

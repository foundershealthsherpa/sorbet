#include "main/lsp/LSPQuery.h"
#include "common/sort.h"
#include "core/FileHash.h"
#include "main/lsp/json_types.h"

using namespace std;

namespace sorbet::realmain::lsp {

// Filter for untyped locations, and dedup responses that are at the same location
vector<unique_ptr<core::lsp::QueryResponse>>
LSPQuery::filterAndDedup(const core::GlobalState &gs,
                         const vector<unique_ptr<core::lsp::QueryResponse>> &queryResponses) {
    vector<unique_ptr<core::lsp::QueryResponse>> responses;
    // Filter for responses with a loc that exists and points to a typed file, unless it's a const, field or
    // definition in which case we're ok with untyped files (because we know where those things are even in untyped
    // files.)
    for (auto &q : queryResponses) {
        core::Loc loc = q->getLoc();
        if (loc.exists() && loc.file().exists()) {
            auto fileIsTyped = loc.file().data(gs).strictLevel >= core::StrictLevel::True;
            // If file is untyped, only support responses involving constants and definitions.
            if (fileIsTyped || q->isConstant() || q->isField() || q->isMethodDef()) {
                responses.push_back(make_unique<core::lsp::QueryResponse>(*q));
            }
        }
    }

    // sort by location and deduplicate
    fast_sort(responses,
              [](const unique_ptr<core::lsp::QueryResponse> &a, const unique_ptr<core::lsp::QueryResponse> &b) -> bool {
                  auto aLoc = a->getLoc();
                  auto bLoc = b->getLoc();
                  int cmp = aLoc.file().id() - bLoc.file().id();
                  if (cmp == 0) {
                      cmp = aLoc.beginPos() - bLoc.beginPos();
                  }
                  if (cmp == 0) {
                      cmp = aLoc.endPos() - bLoc.endPos();
                  }
                  // TODO: precedence based on response type, in case of same location?
                  return cmp < 0;
              });
    responses.resize(
        std::distance(responses.begin(), std::unique(responses.begin(), responses.end(),
                                                     [](const unique_ptr<core::lsp::QueryResponse> &a,
                                                        const unique_ptr<core::lsp::QueryResponse> &b) -> bool {
                                                         auto aLoc = a->getLoc();
                                                         auto bLoc = b->getLoc();
                                                         return aLoc == bLoc;
                                                     })));
    return responses;
}

LSPQueryResult LSPQuery::byLoc(const LSPConfiguration &config, LSPTypecheckerInterface &typechecker, string_view uri,
                               const Position &pos, LSPMethod forMethod, bool errorIfFileIsUntyped) {
    Timer timeit(config.logger, "setupLSPQueryByLoc");
    const core::GlobalState &gs = typechecker.state();
    auto fref = config.uri2FileRef(gs, uri);

    if (!fref.exists() && config.isFileIgnored(config.remoteName2Local(uri))) {
        auto error = make_unique<ResponseError>(
            (int)LSPErrorCodes::InvalidParams,
            fmt::format("Ignored file at uri {} in {}", uri, convertLSPMethodToString(forMethod)));
        return LSPQueryResult{{}, move(error)};
    }

    if (!fref.exists()) {
        auto error = make_unique<ResponseError>(
            (int)LSPErrorCodes::InvalidParams,
            fmt::format("Did not find file at uri {} in {}", uri, convertLSPMethodToString(forMethod)));
        return LSPQueryResult{{}, move(error)};
    }

    if (errorIfFileIsUntyped && fref.data(gs).strictLevel < core::StrictLevel::True) {
        config.logger->info("Ignoring request on untyped file `{}`", uri);
        // Act as if the query returned no results.
        return LSPQueryResult{{}, nullptr};
    }

    auto loc = pos.toLoc(gs, fref);
    if (!loc.has_value()) {
        if (typechecker.isStale()) {
            // File location was not valid, but it's _probably_ not the client's fault--instead it's
            // likely that we're not running on an up-to-date file yet. Since we don't have a good
            // way to check, let's just say "no results"
            return LSPQueryResult{{}, nullptr};
        } else {
            auto error = make_unique<ResponseError>(
                (int)LSPErrorCodes::InvalidParams,
                fmt::format("Position {} in {} does not correspond to a valid location", pos.showRaw(), uri));
            return LSPQueryResult{{}, move(error)};
        }
    }

    return typechecker.query(core::lsp::Query::createLocQuery(loc.value()), {fref});
}

LSPQueryResult LSPQuery::LSPQuery::bySymbolInFiles(const LSPConfiguration &config, LSPTypecheckerInterface &typechecker,
                                                   core::SymbolRef symbol, vector<core::FileRef> frefs) {
    Timer timeit(config.logger, "setupLSPQueryBySymbolInFiles");
    ENFORCE(symbol.exists());
    return typechecker.query(core::lsp::Query::createSymbolQuery(symbol), frefs);
}

LSPQueryResult LSPQuery::bySymbol(const LSPConfiguration &config, LSPTypecheckerInterface &typechecker,
                                  core::SymbolRef symbol) {
    Timer timeit(config.logger, "setupLSPQueryBySymbol");
    ENFORCE(symbol.exists());
    vector<core::FileRef> frefs;
    const core::GlobalState &gs = typechecker.state();
    const core::ShortNameHash symShortNameHash(gs, symbol.name(gs));
    // Locate files that contain the same Name as the symbol. Is an overapproximation, but a good first filter.
    int i = -1;
    for (auto &file : typechecker.state().getFiles()) {
        i++;
        if (file == nullptr) {
            continue;
        }

        ENFORCE(file->getFileHash() != nullptr);
        const auto &hash = *file->getFileHash();
        const auto &usedSymbolNameHashes = hash.usages.nameHashes;
        auto ref = core::FileRef(i);

        const bool fileIsValid = ref.exists() && ref.data(gs).sourceType == core::File::Type::Normal;
        if (fileIsValid && (absl::c_find(usedSymbolNameHashes, symShortNameHash) != usedSymbolNameHashes.end())) {
            frefs.emplace_back(ref);
        }
    }

    return typechecker.query(core::lsp::Query::createSymbolQuery(symbol), frefs);
}

} // namespace sorbet::realmain::lsp

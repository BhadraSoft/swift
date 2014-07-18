//===--- ClangDiagnosticConsumer.cpp - Handle Clang diagnostics -----------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "ClangDiagnosticConsumer.h"
#include "ImporterImpl.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/DiagnosticsClangImporter.h"
#include "clang/AST/ASTContext.h"
#include "clang/Lex/LexDiagnostic.h"

using namespace swift;

ClangDiagnosticConsumer::ClangDiagnosticConsumer(
    ClangImporter::Implementation &impl,
    clang::DiagnosticOptions &clangDiagOptions,
    bool dumpToStderr)
  : TextDiagnosticPrinter(llvm::errs(), &clangDiagOptions),
    ImporterImpl(impl), DumpToStderr(dumpToStderr) {}

void ClangDiagnosticConsumer::HandleDiagnostic(
    clang::DiagnosticsEngine::Level clangDiagLevel,
    const clang::Diagnostic &clangDiag) {
  // Handle the module-not-found diagnostic specially if it's a top-level module
  // we're looking for.
  if (clangDiag.getID() == clang::diag::err_module_not_found &&
      CurrentImport && clangDiag.getArgStdStr(0) == CurrentImport->getName()) {
    setCurrentImportMissing();
    return;
  }

  clang::SourceManager &clangSrcMgr = clangDiag.getSourceManager();
  clang::SourceLocation clangLoc = clangDiag.getLocation();
  clangLoc = clangSrcMgr.getFileLoc(clangLoc);
  SourceLoc loc = ImporterImpl.importSourceLoc(clangLoc);

  auto &ctx = ImporterImpl.SwiftContext;

  if (clangDiag.getID() == clang::diag::err_module_not_built &&
      CurrentImport && clangDiag.getArgStdStr(0) == CurrentImport->getName()) {
    ctx.Diags.diagnose(loc, diag::clang_cannot_build_module,
                       CurrentImport->getName());
    return;
  }

  // Satisfy the default implementation (bookkeeping).
  if (DumpToStderr)
    TextDiagnosticPrinter::HandleDiagnostic(clangDiagLevel, clangDiag);
  else
    DiagnosticConsumer::HandleDiagnostic(clangDiagLevel, clangDiag);

  decltype(diag::error_from_clang) diagKind;
  switch (clangDiagLevel) {
  case clang::DiagnosticsEngine::Ignored:
    return;
  case clang::DiagnosticsEngine::Note:
    diagKind = diag::note_from_clang;
    break;
  case clang::DiagnosticsEngine::Remark:
    // FIXME: We don't handle remarks yet.
    return;
  case clang::DiagnosticsEngine::Warning:
    diagKind = diag::warning_from_clang;
    break;
  case clang::DiagnosticsEngine::Error:
  case clang::DiagnosticsEngine::Fatal:
    // FIXME: What happens after a fatal error in the importer?
    diagKind = diag::error_from_clang;
    break;
  }

  llvm::SmallString<128> message;

  // FIXME: Until we have real source locations, they're included in the
  // diagnostic.
  // Note: getPresumedLoc handles invalid source locations by returning an
  // empty PresumedLoc structure. In these cases we just won't show location
  // information.
  auto presumedLoc = clangSrcMgr.getPresumedLoc(clangLoc);
  if (presumedLoc.getFilename()) {
    message.append(presumedLoc.getFilename());
    message.push_back(':');
    llvm::raw_svector_ostream(message) << presumedLoc.getLine();
    message.append(": ");
  }

  clangDiag.FormatDiagnostic(message);

  // FIXME: Include source ranges in the diagnostic.
  ctx.Diags.diagnose(loc, diagKind, message);
}

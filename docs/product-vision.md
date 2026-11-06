# Product vision and purpose

Artist-defined direction · updated 6 November 2026

**Start with a drawing. Build toward hand-drawn anime.**

Toon Pro exists to give independent artists an approachable way into drawing and animation, with room for the organization and technical depth that more demanding work needs. The artist remains responsible for the drawings, timing, color and decisions that give the work its character.

The ambition is a coherent desktop workflow for creating hand-drawn anime. It is a direction for development, rather than a claim that a complete production pipeline or studio-scale acceptance has been delivered.

## The problem we are solving

The first session should answer a simple question: “Where can I draw?” A beginner should not have to plan a film, understand a scene hierarchy or configure a production system before making that first image.

As a project grows, the questions change. Which drawing belongs to this shot? Where is the background reference? Can I keep two documents open while preserving their undo history? Can I move editable artwork into the asset collection without flattening it?

Toon Pro's current local architecture addresses those concerns by separating project management from focused authoring, while keeping both in one application.

## Who it is for

- Illustrators who want a direct route to a canvas and a place to organize their drawings.
- Artists learning frame-by-frame animation, one pose and one shot at a time.
- Independent anime creators who need drawings, animation and reference materials organized together.
- Experienced artists who want to evaluate a developing native workflow and contribute concrete feedback.

Existing production teams should evaluate file compatibility, performance, collaboration and delivery requirements against their own pipeline. A mature studio integration layer is not implied by the product's name or ambition.

## The progression

| Stage | Artist's task | Toon Pro's role | Status |
| --- | --- | --- | --- |
| **Make a drawing** | Choose a canvas and begin sketching or painting | Drawing projects, preview-based setup, layers, tools and retained editor tabs | Implemented in the current local managed flow |
| **Explore movement** | Develop poses, holds and motion inside one shot | Independent animation documents, frame-by-frame tools, shot-local timing and output | Implemented locally; engine workflows have individual documented limits |
| **Organize a larger idea** | Collect backgrounds, character sheets, shots and references | Movie folders, tags and a separate Assets collection | Implemented locally; this is an organizer, not a complete sequence editor |
| **Support production handoffs** | Connect story, boards, animatics, finishing and review | Deeper production workflows built around editable sources and deliberate decisions | Direction; not a delivered end-to-end pipeline |
| **Deliver reliably at scale** | Validate long projects, packages and production output | Measured resource budgets, platform acceptance and validated delivery | Ongoing acceptance work and future development |

“Production-level” describes the standard we want to work toward. It is not a guarantee of an artist's final image quality, a benchmark result, or a completed movie pipeline.

## Product principles

### Begin with the smallest useful task

Launch into Projects. A Drawing project offers drawings and New drawing. An Animation project offers independent shots. A Movie project adds flexible folders and tags. The artist chooses the level of organization that fits the task.

### Keep authoring focused

Each editor represents one drawing or one animated shot. Project collections and asset organization belong in dashboards. Several documents can remain open in tabs, preserving independent history, timing and canvas state.

### Put the artwork first

Frequent actions stay close to the work. Compact neutral interface surfaces leave color and attention for the artist's images. Direct manipulation, visual previews, recognizable tools and precise controls help make settings understandable.

### Keep source data meaningful

Moving a native item into Assets changes its organization while retaining its identity and editable source. An imported original is copied into its project. The UI must distinguish an editable document, an imported file, an independent copy and any future linked instance.

### Let projects live in ordinary folders

The current managed project is a `.toonstudio` folder with a catalog, independent native document revisions and project-owned media. There is no reserved disk partition. A local project folder can be inspected and backed up using normal filesystem workflows; exact operating and recovery limits still apply.

### Describe the delivered product honestly

Document what the controls actually do. Label current local development, published packages and future plans separately. Link verification evidence to a specific build and workflow. Do not use invented customer counts, testimonials, output-quality claims or comparisons to imply acceptance that has not happened.

## Where Toon Pro fits

The useful distinction is the combination of a drawing-first start, independent single-shot authoring and separate local organization. This can suit artists who want to grow their practice without putting film-management controls into every canvas session.

Other applications may be the stronger choice for a particular drawing tradition, an established studio pipeline, a broad 3D scene workflow, specialist compositing, collaboration or an existing interchange requirement. The [comparison](product-comparison.md) explains documented differences rather than declaring a universal winner.

## Current boundaries

The local managed flow has project dashboards, document tabs, folder/tag organization, original-media import, previews, Move to Assets, guarded saves, background recovery and manual saved-project backup. It does not establish unlimited project size, crash-proof storage, network collaboration, a global library, live linked placement or complete production tracking.

Physical tablet feel, production-scale memory/playback, additional platform packages and broader disk-failure recovery need dedicated acceptance work. Legacy collection migration is excluded from this development scope at the artist's request.

The published **0.75.0** preview uses the earlier workflow. The new **0.76.0** managed project flow is local development and has not replaced those downloads. Pricing and application licensing remain undecided.

[Create your first drawing](getting-started.md) · [Implemented behavior and limits](managed-workspace.md) · [Development checklist](TODO.md) · [Website vision page source](../website/templates/vision.html)

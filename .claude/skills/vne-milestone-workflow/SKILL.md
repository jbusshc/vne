---
name: vne-milestone-workflow
description: Procedimiento de trabajo por hitos en vne — cómo empezar un hito, qué hacer durante, cómo cerrarlo, la definición de terminado y el registro de decisiones. Consulta este skill SIEMPRE al empezar a trabajar en el proyecto, al recibir la instrucción de implementar un hito (M0, M1, M2...), al terminar un bloque de trabajo, antes de dar algo por completado, o cuando dudes si puedes avanzar al siguiente hito. Úsalo también si te encuentras a punto de tomar una decisión de diseño que no está en la especificación.
---

# Flujo de trabajo por hitos

El proyecto avanza en hitos secuenciales. Cada uno termina con un ejecutable que funciona.
Los criterios de aceptación están en `docs/SPEC.md` §12 y son medibles, no opinables.

## Al empezar un hito

1. Lee la sección del hito en `docs/SPEC.md` §12 completa, incluidos sus criterios.
2. Lee los skills relevantes al área que vas a tocar.
3. Actualiza la línea "Hito activo" en `CLAUDE.md`.
4. Escribe un plan corto: qué archivos vas a crear, en qué orden, y cómo vas a verificar cada
   criterio de aceptación. Preséntalo antes de empezar a escribir código.
5. Si el plan requiere una decisión que no está en la especificación, **párate y pregunta
   ahora**, no a mitad de la implementación.

## Durante el hito

- No trabajes fuera del alcance del hito. Si ves algo que arreglar de un hito futuro, anótalo
  en `docs/DECISIONS.md` bajo "Pendientes observados" y sigue.
- No dejes `TODO` sin registrar. Un `TODO` en el código debe tener una entrada
  correspondiente en `docs/DECISIONS.md`.
- Compila con frecuencia. No acumules mil líneas sin compilar.
- Si un criterio de aceptación resulta imposible o mal planteado, dilo en cuanto lo
  descubras. No lo redefinas por tu cuenta para que pase.

## Definición de terminado

Un hito está cerrado cuando **todas** estas condiciones se cumplen:

1. Compila limpio con `-Wall -Wextra -Werror` en las plataformas de prioridad 1 y 2
   (Windows, Linux, macOS).
2. Pasa bajo AddressSanitizer y UndefinedBehaviorSanitizer sin reportes.
3. Cumple todos los criterios de aceptación del hito, **verificados de forma explícita**, no
   asumidos.
4. Sus tests están en `tests/` y pasan.
5. `docs/DECISIONS.md` está actualizado.
6. El contador de asignaciones de heap por frame sigue en cero.

Si alguna condición no se puede verificar en el entorno disponible (por ejemplo, no hay
macOS), **dilo explícitamente** en el resumen de cierre. No la marques como cumplida.

## Al cerrar el hito

Entrega un resumen corto con estos cinco puntos, sin adornos:

- **Qué se implementó** — lista de módulos y archivos.
- **Criterios verificados** — cada criterio con el método de verificación y el resultado
  concreto (números, no adjetivos).
- **Qué quedó pendiente** — y por qué.
- **Decisiones registradas** — referencia a las entradas nuevas de `DECISIONS.md`.
- **Riesgos detectados** — cualquier cosa que complique un hito futuro.

Después actualiza `CLAUDE.md` ("Último hito completado") y **espera confirmación antes de
empezar el siguiente**.

## Registro de decisiones

`docs/DECISIONS.md` es un registro append-only. Una entrada por decisión no trivial:

```markdown
## ADR-00NN — Título corto

**Fecha:** AAAA-MM-DD
**Hito:** M2
**Estado:** aceptada | sustituida por ADR-00XX

**Contexto.** Qué problema apareció.

**Decisión.** Qué se eligió.

**Alternativas descartadas.** Qué más se consideró y por qué no.

**Consecuencias.** Qué se vuelve más fácil y qué más difícil a partir de ahora.
```

Registra: cualquier ambigüedad de la especificación que resolviste, cualquier elección de
formato de datos, cualquier compromiso de rendimiento, y cualquier constante cuyo valor no
sea obvio.

**No registres** cambios triviales ni decisiones que ya estén en `docs/SPEC.md`.

## Cuando la especificación se queda corta

Orden de actuación:

1. ¿La respuesta está en `docs/SPEC.md` o en un skill? Úsala.
2. ¿Hay una opción claramente más simple? Tómala y regístrala como ADR.
3. ¿Es una decisión con consecuencias arquitectónicas o de formato en disco? **Párate y
   pregunta.**

Las decisiones que el agente no debe tomar solo están listadas en `docs/SPEC.md` §14.

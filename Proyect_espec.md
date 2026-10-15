# PROMPT MAESTRO — MiniDB: Database Engine From Scratch

## 1. Rol y contexto

Actúa como un **Senior Database Engine Engineer**, especialista en programación de sistemas en C, estructuras de datos, almacenamiento persistente, índices B+ Tree, gestión de memoria, procesamiento de consultas y arquitectura interna de sistemas gestores de bases de datos.

También actuarás como mi mentor técnico durante el desarrollo.

Soy estudiante de Ingeniería en Computación de la Facultad de Ingenieria UNAM y mi objetivo profesional es prepararme para una pasantía en Oracle Mexico Development Center (MDC), Guadalajara, particularmente en áreas relacionadas con el desarrollo del motor de Oracle Database.

Quiero desarrollar un proyecto de portafolio profesional llamado **MiniDB**, un motor de base de datos relacional educativo, construido desde cero en C.

El proyecto debe demostrar habilidades reales en:
- Programación en C y gestión manual de memoria.
- Estructuras de datos y algoritmos.
- Almacenamiento binario persistente.
- Organización de archivos en páginas.
- Implementación de índices B+ Tree.
- Interpretación y ejecución de consultas.
- Optimización de búsquedas.
- Manejo de errores y depuración.
- Pruebas automatizadas y benchmarking.
- Diseño modular y documentación técnica.

No quiero un simple CRUD, ni una aplicación que utilice SQLite u otro motor existente como backend. **Quiero implementar los componentes fundamentales del motor.**

## 2. Entorno de desarrollo

Mi entorno es el siguiente:

- Sistema operativo: Arch Linux.
- Shell: Fish.
- Lenguaje principal: C17.
- Compilador: GCC.
- Sistema de construcción: CMake.
- Editor: Visual Studio Code.
- Control de versiones: Git.
- Depurador: GDB.
- Herramientas adicionales: Valgrind, AddressSanitizer y UndefinedBehaviorSanitizer cuando corresponda.

Prioriza herramientas gratuitas, de código abierto y disponibles en Arch Linux.

No introduzcas dependencias innecesarias.

Utiliza bibliotecas estándar de C y las interfaces POSIX necesarias para trabajar con archivos, justificando cualquier dependencia específica de Linux.

## 3. Objetivo general

Construir una base de datos ejecutable desde terminal, capaz de:

1. Recibir comandos mediante una interfaz interactiva.
2. Interpretar instrucciones de inserción, búsqueda, actualización y eliminación.
3. Almacenar registros de forma persistente en un archivo binario.
4. Organizar el almacenamiento en páginas de tamaño fijo.
5. Implementar una estructura B+ Tree para indexar registros.
6. Recuperar registros mediante búsquedas eficientes.
7. Ejecutar consultas sencillas y recorridos ordenados.
8. Mantener la integridad estructural del archivo de datos.
9. Validar operaciones mediante pruebas automatizadas.
10. Medir el rendimiento del motor con benchmarks reproducibles.

El proyecto será educativo y no deberá presentarse como equivalente a un sistema de producción.

No es necesario implementar todas estas capacidades inmediatamente. Deben desarrollarse progresivamente.

## 4. Interfaz esperada

El motor se iniciará desde la terminal:

    ./minidb database.db

Ejemplo de sesión:

    MiniDB v0.1
    Educational Database Engine

    minidb> INSERT 1 "Alexis" 21
    OK: 1 row inserted

    minidb> INSERT 2 "Carlos" 22
    OK: 1 row inserted

    minidb> SELECT 1
    1 | Alexis | 21

    minidb> SELECT_ALL
    1 | Alexis | 21
    2 | Carlos | 22

    minidb> UPDATE 1 "Alexis Perez" 22
    OK: 1 row updated

    minidb> DELETE 2
    OK: 1 row deleted

    minidb> .stats
    Database statistics...

    minidb> .btree
    B+ Tree structure...

    minidb> .exit

La sintaxis anterior será una interfaz propia simplificada, no SQL estándar.

Los comandos INSERT, SELECT y SELECT_ALL son obligatorios para la primera versión funcional. UPDATE y DELETE deberán incorporarse después de que la estructura B+ Tree básica sea estable.

## 5. Arquitectura del software

Organiza el proyecto en módulos con responsabilidades bien definidas.

La arquitectura objetivo será:

### 5.1 REPL — Read-Eval-Print Loop

Responsabilidades:
- Leer comandos desde la terminal.
- Mostrar el prompt interactivo.
- Identificar comandos especiales.
- Manejar el cierre del programa.
- Detectar entradas demasiado largas y errores de lectura.

### 5.2 Parser

Responsabilidades:
- Interpretar los comandos.
- Extraer argumentos.
- Validar tipos y límites.
- Reconocer cadenas entre comillas.
- Rechazar instrucciones mal formadas.
- Producir una representación interna de cada operación.

No utilices directamente funciones inseguras de lectura ni asumas que toda entrada es válida.

### 5.3 Execution Engine

Responsabilidades:
- Ejecutar operaciones INSERT, SELECT y SELECT_ALL.
- Interactuar con el almacenamiento y los índices.
- Devolver resultados y errores estructurados.
- Mantener separadas la interpretación y la ejecución.

### 5.4 Storage Engine

Responsabilidades:
- Crear y abrir archivos de bases de datos.
- Serializar y deserializar registros.
- Leer y escribir páginas binarias.
- Administrar posiciones dentro de las páginas.
- Detectar archivos inválidos o incompatibles.

### 5.5 Pager

Implementa un administrador de páginas.

Características:
- Tamaño de página fijo de 4096 bytes.
- Identificadores de página.
- Lectura y escritura mediante desplazamientos de archivo.
- Caché de páginas en memoria.
- Seguimiento de páginas modificadas.
- Operaciones de sincronización y cierre.
- Validación de límites.

Comienza con una implementación sencilla y correcta. No introduzcas una política compleja de reemplazo de caché hasta que sea necesaria.

### 5.6 B+ Tree Index

Implementa un índice B+ Tree persistente.

Debe incluir:
- Nodos hoja.
- Nodos internos.
- Claves ordenadas.
- Búsqueda por clave.
- Inserción ordenada.
- División de nodos hoja.
- División de nodos internos.
- Propagación de separadores.
- División de la raíz.
- Recorridos ordenados.
- Validación de invariantes estructurales.

Los registros se identificarán inicialmente mediante un ID entero único.

Documenta claramente cómo están organizados los nodos y qué datos se almacenan en ellos.

### 5.7 Testing and Benchmarking

Incluye:
- Pruebas unitarias.
- Pruebas de integración.
- Pruebas de persistencia.
- Pruebas de casos límite.
- Pruebas de inserción aleatoria.
- Pruebas de integridad del B+ Tree.
- Herramientas de detección de errores de memoria.
- Experimentos reproducibles de rendimiento.

## 6. Diseño de almacenamiento

Para comenzar, cada registro tendrá:

- `id`: entero sin signo de 32 bits.
- `name`: cadena UTF-8 con un máximo documentado de 64 bytes.
- `age`: entero sin signo, validado dentro de un rango razonable.

Define explícitamente el formato de serialización.

Evita escribir directamente una estructura C completa en disco mediante `fwrite(&record, sizeof(record), ...)`, porque el relleno de memoria, la alineación y la representación de datos pueden hacer que el formato sea dependiente de la plataforma.

El archivo de base de datos deberá contar con metadatos como:
- Identificador mágico del archivo.
- Versión del formato.
- Tamaño de página.
- Número o identificador de la página raíz.
- Metadatos necesarios para gestionar las páginas.

Especifica:
- Endianness del formato.
- Offsets y tamaños de cada campo.
- Identificadores de página válidos.
- Comportamiento cuando el archivo está vacío.
- Detección de corrupción básica.
- Política de sincronización de datos.

No prometas atomicidad ante caídas del sistema ni recuperación completa si todavía no implementamos un mecanismo transaccional.

## 7. Etapas obligatorias del desarrollo

Divide el desarrollo en las siguientes fases.

### FASE 1 — Fundamentos y CLI

Objetivo: crear la estructura inicial del proyecto.

Tareas:
1. Preparar CMake.
2. Crear los directorios del proyecto.
3. Implementar el REPL.
4. Implementar `.help` y `.exit`.
5. Crear el módulo Parser.
6. Interpretar comandos básicos.
7. Validar errores de entrada.
8. Implementar las primeras pruebas.
9. Configurar opciones estrictas de compilación.

Resultado esperado: aplicación funcional que interpreta comandos, aunque todavía no almacene registros.

### FASE 2 — Storage Engine

Objetivo: guardar información persistentemente.

Tareas:
1. Diseñar el formato binario.
2. Crear funciones de serialización.
3. Implementar apertura y cierre de archivos.
4. Implementar lectura y escritura de páginas.
5. Implementar el Pager.
6. Guardar registros.
7. Recuperar registros después de reiniciar el programa.
8. Validar archivos dañados o incompatibles.
9. Añadir pruebas de persistencia.

Inicialmente, permite búsquedas secuenciales sin índices.

Resultado esperado: insertar registros, cerrar MiniDB, reabrirlo y recuperar correctamente los datos.

### FASE 3 — B+ Tree

Objetivo: construir un índice persistente.

Tareas:
1. Diseñar los nodos internos y hojas.
2. Implementar la búsqueda dentro de una hoja.
3. Implementar la localización de hojas.
4. Implementar inserciones.
5. Implementar división de nodos hoja.
6. Implementar división de nodos internos.
7. Gestionar divisiones de la raíz.
8. Implementar recorridos ordenados.
9. Persistir el árbol.
10. Verificar invariantes mediante pruebas.

Resultado esperado: insertar miles de registros en orden aleatorio, buscar por ID y recuperar los registros después de reiniciar MiniDB.

### FASE 4 — Query Engine

Objetivo: integrar el índice y completar el conjunto básico de operaciones.

Tareas:
1. Implementar SELECT por ID.
2. Implementar SELECT_ALL ordenado.
3. Añadir filtros sencillos cuando sean convenientes.
4. Implementar UPDATE.
5. Implementar DELETE con una estrategia claramente documentada.
6. Implementar `.stats`.
7. Implementar `.btree`.

Para DELETE, analiza primero si conviene implementar borrado físico con rebalanceo o un mecanismo educativo de borrado lógico. No presentes un borrado lógico como una implementación completa de eliminación y recuperación de espacio.

Resultado esperado: motor persistente con operaciones funcionales y una interfaz que permita inspeccionar sus estructuras.

### FASE 5 — Testing, Benchmarking y calidad

Objetivo: convertir el prototipo en un proyecto presentable profesionalmente.

Tareas:
1. Crear pruebas unitarias con CTest.
2. Verificar inserciones duplicadas.
3. Probar claves inexistentes.
4. Probar entradas incorrectas.
5. Verificar persistencia después de reiniciar.
6. Validar índices con inserciones aleatorias.
7. Ejecutar pruebas con 10,000 y 100,000 registros.
8. Comparar búsquedas secuenciales contra búsquedas indexadas.
9. Analizar uso de memoria.
10. Ejecutar sanitizers.
11. Documentar limitaciones.
12. Preparar benchmarks con condiciones reproducibles.

No inventes resultados de rendimiento. Ejecuta mediciones reales y registra sus condiciones.

## 8. Estructura de directorios propuesta

    minidb/
    ├── CMakeLists.txt
    ├── README.md
    ├── .gitignore
    ├── include/
    │   ├── repl.h
    │   ├── parser.h
    │   ├── executor.h
    │   ├── pager.h
    │   ├── storage.h
    │   ├── record.h
    │   └── btree.h
    ├── src/
    │   ├── main.c
    │   ├── repl.c
    │   ├── parser.c
    │   ├── executor.c
    │   ├── pager.c
    │   ├── storage.c
    │   ├── record.c
    │   └── btree.c
    ├── tests/
    ├── benchmarks/
    ├── docs/
    │   ├── architecture.md
    │   ├── storage-format.md
    │   ├── btree-design.md
    │   └── engineering-log.md
    └── .github/
        └── workflows/

Esta es la estructura objetivo, no una obligación de crear todos los archivos vacíos desde el primer día.

## 9. Requisitos de calidad

Sigue estas reglas durante todo el desarrollo:

- Utiliza C17.
- Evita variables globales innecesarias.
- Define claramente la propiedad y duración de la memoria.
- Comprueba todas las asignaciones dinámicas.
- Comprueba resultados de lectura y escritura.
- Evita desbordamientos de búfer.
- Evita comportamientos indefinidos.
- Evita pérdidas de memoria.
- No ignores errores de compilación.
- Usa `-Wall`, `-Wextra` y `-Wpedantic`.
- Configura builds con sanitizers.
- Implementa funciones pequeñas y con responsabilidades claras.
- Documenta las invariantes de cada estructura.
- Separa el código de producción del código de pruebas.
- Utiliza nombres descriptivos en inglés.
- Mantén los mensajes de interfaz y la documentación técnica en inglés.
- Explícame los conceptos en español.
- No incluyas código de terceros sin identificar su procedencia y licencia.

## 10. Pruebas de aceptación

Antes de considerar terminada la versión 1.0, deben verificarse al menos estas condiciones:

1. Los registros persisten entre ejecuciones.
2. Las inserciones de IDs duplicados son rechazadas sin corromper datos.
3. Las búsquedas de IDs inexistentes devuelven un resultado controlado.
4. Los recorridos entregan claves en orden ascendente.
5. Las divisiones de nodos no pierden registros.
6. El árbol mantiene sus invariantes después de inserciones aleatorias.
7. El formato de archivo se valida correctamente al abrirlo.
8. Las entradas inválidas no provocan fallos del programa.
9. No existen errores conocidos detectados por AddressSanitizer o UndefinedBehaviorSanitizer en las pruebas ejecutadas.
10. Se pueden insertar y recuperar al menos 100,000 registros dentro de los límites documentados de diseño.

Si una capacidad no está implementada, indícalo explícitamente. No afirmes que el motor cumple requisitos que no se han probado.

## 11. Documentación para GitHub

La documentación será una parte importante del proyecto.

El README deberá incluir:
- Descripción del motor.
- Motivación.
- Características implementadas.
- Arquitectura.
- Tecnologías.
- Requisitos de compilación.
- Instrucciones de instalación.
- Ejemplos de uso.
- Explicación del formato de almacenamiento.
- Diseño del B+ Tree.
- Pruebas ejecutadas.
- Benchmarks.
- Limitaciones conocidas.
- Futuras mejoras.

También mantendremos `docs/engineering-log.md` como una bitácora de ingeniería que explique las decisiones importantes del proyecto.

No redactes la documentación como si se hubieran completado funcionalidades que todavía están pendientes.

## 12. Forma en que trabajaremos

Quiero que actúes como desarrollador y mentor, no solamente como generador de código.

Para cada etapa:

1. Explica el objetivo técnico y qué problema resuelve.
2. Explica los conceptos necesarios para comprender la implementación.
3. Propón una arquitectura sencilla y justifica las decisiones.
4. Implementa el código en los archivos correspondientes.
5. Explica las funciones, estructuras y algoritmos importantes.
6. Compila y ejecuta el proyecto cuando el entorno lo permita.
7. Implementa y ejecuta pruebas.
8. Analiza los errores encontrados y corrígelos.
9. Resume lo que se implementó.
10. Explica las limitaciones actuales.
11. Propón un mensaje descriptivo de Git commit.
12. Indica cuál será el siguiente objetivo técnico.

No avances automáticamente a la siguiente fase principal sin que yo lo solicite.

No desarrolles las cinco fases completas en una sola respuesta.

Prefiero una implementación incremental, entendible y bien probada, en lugar de cientos de líneas de código generadas apresuradamente.

Cuando exista más de una alternativa técnica, explícame brevemente sus ventajas, desventajas y por qué elegirías una.

Evita sobreingeniería, abstracciones innecesarias y soluciones que oculten el funcionamiento interno del motor.

Si encuentras errores en mi planteamiento, corrígelos y justifica el cambio.

## 13. Uso de Git

Trabajaremos con Git desde el principio.

- No sobrescribas trabajo existente sin revisarlo.
- Inspecciona el estado del repositorio antes de modificarlo.
- Propón commits pequeños con nombres descriptivos.
- Mantén el `.gitignore` actualizado.
- No agregues binarios, archivos temporales ni bases de datos de prueba al repositorio.
- No crees ni publiques repositorios remotos sin mi autorización.
- No hagas commits automáticamente a menos que te lo indique.

## 14. Límites del alcance

MiniDB versión 1.0 será un motor educativo de una sola tabla, con registros de esquema fijo y un índice principal persistente.

No implementaremos inicialmente:
- SQL completo.
- Joins.
- Optimización de consultas basada en costos.
- Transacciones ACID completas.
- Write-Ahead Logging.
- Recuperación ante fallos a mitad de escritura.
- Replicación.
- Procesamiento distribuido.
- Escrituras concurrentes de múltiples procesos.
- Control de acceso o autenticación.
- Compatibilidad con archivos de Oracle Database.

Estas características podrán estudiarse en versiones posteriores o en proyectos independientes.

Evita afirmar que MiniDB es seguro ante cortes de energía o fallos inesperados mientras no exista una estrategia validada de recuperación.

## 15. Resultado final deseado

Al finalizar quiero tener un proyecto de GitHub que demuestre:

- Que puedo programar software de sistemas en C.
- Que comprendo cómo se representan los datos en disco.
- Que puedo diseñar estructuras de datos persistentes.
- Que puedo implementar un índice B+ Tree.
- Que entiendo la relación entre almacenamiento, indexación y ejecución de consultas.
- Que sé detectar errores, depurar y realizar pruebas.
- Que puedo justificar decisiones de diseño mediante experimentos.
- Que puedo desarrollar y documentar un proyecto completo de ingeniería.

El proyecto debe ser suficientemente claro y sólido para poder explicarlo durante una entrevista técnica de Software Engineering o Database Engine Development.

## 16. Primera tarea

Comenzaremos exclusivamente con la FASE 1.

Antes de implementar:

1. Inspecciona el directorio actual y comprueba si existe algún código previo de MiniDB.
2. Verifica qué herramientas de compilación están disponibles.
3. Presenta brevemente la arquitectura inicial.
4. Prepara CMake y la estructura mínima necesaria.
5. Implementa el REPL y un parser inicial.
6. Implementa `.help` y `.exit`.
7. Añade pruebas básicas para las entradas válidas e inválidas.
8. Compila el proyecto y ejecuta las pruebas.
9. Explícame cómo ejecutarlo y depurarlo desde Arch Linux.
10. Presenta los resultados reales de las verificaciones y los archivos modificados.

Si no puedes ejecutar algún comando, indícalo en lugar de asumir que funcionó.

Una vez completada la FASE 1, detente y presenta un resumen de lo aprendido, las decisiones de diseño y las siguientes tareas.

**Comienza ahora con el análisis y la implementación de la FASE 1 de MiniDB.**
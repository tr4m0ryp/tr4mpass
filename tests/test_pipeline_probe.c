/*
 * test_pipeline_probe.c -- Pipeline module probe selection logic.
 *
 * Tests the probe selection against mock modules that replicate path_a
 * and path_b probe conditions without requiring any hardware linkage.
 */

#include "test_framework.h"

#include "pipeline/pipeline.h"
#include "device/device.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Mock pipeline modules                                                */
/* ------------------------------------------------------------------ */

static int mock_probe_path_a(device_info_t *dev)
{
    if (!dev) return -1;
    if (dev->checkm8_compatible != 1) return 0;
    if (dev->is_dfu_mode != 1) return 0;
    return 1;
}

static int mock_probe_path_b(device_info_t *dev)
{
    if (!dev) return -1;
    if (dev->checkm8_compatible != 0) return 0;
    if (dev->is_dfu_mode != 1) return 0;
    return 1;
}

static int mock_execute_noop(device_info_t *dev) { (void)dev; return 0; }

static const pipeline_module_t mock_path_a = {
    .name        = "mock_path_a",
    .description = "Mock Path A for testing",
    .probe       = mock_probe_path_a,
    .execute     = mock_execute_noop
};

static const pipeline_module_t mock_path_b = {
    .name        = "mock_path_b",
    .description = "Mock Path B for testing",
    .probe       = mock_probe_path_b,
    .execute     = mock_execute_noop
};

static device_info_t make_dev(int checkm8_vuln, int is_dfu)
{
    device_info_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.checkm8_compatible = checkm8_vuln;
    dev.is_dfu_mode        = is_dfu;
    return dev;
}

/* ------------------------------------------------------------------ */
/* Cases                                                              */
/* ------------------------------------------------------------------ */

static void test_probe_path_a_compatible(void)
{
    device_info_t dev = make_dev(1, 1);
    ASSERT_EQ(mock_probe_path_a(&dev), 1);
}

static void test_probe_path_a_not_dfu(void)
{
    device_info_t dev = make_dev(1, 0);
    ASSERT_EQ(mock_probe_path_a(&dev), 0);
}

static void test_probe_path_a_a12(void)
{
    device_info_t dev = make_dev(0, 1);
    ASSERT_EQ(mock_probe_path_a(&dev), 0);
}

static void test_probe_path_b_compatible(void)
{
    device_info_t dev = make_dev(0, 1);
    ASSERT_EQ(mock_probe_path_b(&dev), 1);
}

static void test_probe_path_b_a11(void)
{
    device_info_t dev = make_dev(1, 1);
    ASSERT_EQ(mock_probe_path_b(&dev), 0);
}

static void test_probe_path_b_not_dfu(void)
{
    device_info_t dev = make_dev(0, 0);
    ASSERT_EQ(mock_probe_path_b(&dev), 0);
}

static void test_probe_select_prefers_a(void)
{
    /* Both modules registered; checkm8-compatible device -> Path A selected */
    pipeline_register(&mock_path_a);
    pipeline_register(&mock_path_b);

    device_info_t dev = make_dev(1, 1);
    const pipeline_module_t *sel = pipeline_select(&dev);
    ASSERT_NOTNULL(sel);
    ASSERT_EQ(strcmp(sel->name, "mock_path_a"), 0);
}

static void test_probe_select_falls_to_b(void)
{
    /* A12+ device -> Path A probe returns 0, Path B selected */
    device_info_t dev = make_dev(0, 1);
    const pipeline_module_t *sel = pipeline_select(&dev);
    ASSERT_NOTNULL(sel);
    ASSERT_EQ(strcmp(sel->name, "mock_path_b"), 0);
}

static void test_probe_select_no_match(void)
{
    /* Normal mode A12+ device -> neither probe matches */
    device_info_t dev = make_dev(0, 0);
    ASSERT_NULL(pipeline_select(&dev));
}

void run_pipeline_probe_tests(void)
{
    printf("--- Section 3: Pipeline probe logic ---\n");
    test_probe_path_a_compatible();
    test_probe_path_a_not_dfu();
    test_probe_path_a_a12();
    test_probe_path_b_compatible();
    test_probe_path_b_a11();
    test_probe_path_b_not_dfu();
    /* These three share a module registry -- must run after the others */
    test_probe_select_prefers_a();
    test_probe_select_falls_to_b();
    test_probe_select_no_match();
}
